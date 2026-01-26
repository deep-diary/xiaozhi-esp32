#include "app_speech.hpp"
#include <memory>
#include <cstring>
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "soc/soc_caps.h"
#include "esp_err.h"
#include "esp_log.h"

#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "bsp_board_extra.h"

#include "mmap_generate_audio.h"

#define I2S_CHANNEL_NUM 1
#define I2S_CH ((i2s_port_t)1)

static const char *TAG = "App/Speech";

typedef enum {
    AUDIO_WAKENET_START,
    AUDIO_SUCCESS,
    AUDIO_ERROR,
} audio_play_state_t;

static QueueHandle_t        g_queue_audio_play  = NULL;
static mmap_assets_handle_t asset_audio;

static esp_codec_dev_handle_t mic_hdl = NULL;
static esp_codec_dev_handle_t spk_hdl   = NULL;

static esp_err_t i2s_init(uint8_t i2s_num, uint32_t sample_rate, int channel_format, int bits_per_chan)
{
    esp_err_t ret_val = ESP_OK;

    spk_hdl = bsp_extra_audio_codec_speaker_init();
    esp_codec_dev_set_out_vol(spk_hdl, 100);

    esp_codec_dev_sample_info_t fs_spk = {
        .bits_per_sample    = CODEC_DEFAULT_BIT_WIDTH,
        .channel            = 1,
        .sample_rate        = CODEC_DEFAULT_SAMPLE_RATE,
    };

    esp_codec_dev_open(spk_hdl, &fs_spk);

    mic_hdl = bsp_extra_audio_codec_microphone_init();
    esp_codec_dev_sample_info_t fs_mic = {
        .bits_per_sample = CODEC_DEFAULT_BIT_WIDTH,
        .channel = 1,
        .sample_rate = CODEC_DEFAULT_SAMPLE_RATE,
    };
    esp_codec_dev_set_in_gain(mic_hdl, CODEC_DEFAULT_ADC_VOLUME);
    esp_codec_dev_open(mic_hdl, &fs_mic);

    return ret_val;
}

void AppSpeech::audio_play_success(void)
{
    audio_play_state_t audio_result = AUDIO_SUCCESS;
    xQueueSend(g_queue_audio_play, &audio_result, 10);
}

void AppSpeech::audio_play_error(void)
{
    audio_play_state_t audio_result = AUDIO_ERROR;
    xQueueSend(g_queue_audio_play, &audio_result, 10);
}

static void app_sr_mmap_audio()
{
    const mmap_assets_config_t config = {
        .partition_label = "storage",
        .max_files = MMAP_AUDIO_FILES,
        .checksum = MMAP_AUDIO_CHECKSUM,
        .flags = {
            .mmap_enable = true,
            .app_bin_check = true,
        },
    };

    mmap_assets_new(&config, &asset_audio);
    ESP_LOGI(TAG, "stored_files:%d", mmap_assets_get_stored_files(asset_audio));
}

void audio_play_task(void *pvParam)
{
    app_sr_mmap_audio();

    

    ESP_LOGI(TAG, "audio_play_task started");

    while (true) {
        audio_play_state_t result;
        if (xQueueReceive(g_queue_audio_play, &result, portMAX_DELAY) == pdTRUE) {
            switch (result) {
                case AUDIO_WAKENET_START: {
                    ESP_LOGI(TAG, "paly");
                    const void *audio = mmap_assets_get_mem(asset_audio, MMAP_AUDIO_ECHO_WAV);
                    uint32_t len = mmap_assets_get_size(asset_audio, MMAP_AUDIO_ECHO_WAV);
                    esp_codec_dev_write(spk_hdl, (void*)audio, len);
                    break;
                }
                case AUDIO_SUCCESS: {
                    ESP_LOGI(TAG, "success");
                    const void *audio = mmap_assets_get_mem(asset_audio, MMAP_AUDIO_SUCCESS_WAV);
                    uint32_t len = mmap_assets_get_size(asset_audio, MMAP_AUDIO_SUCCESS_WAV);
                    esp_codec_dev_write(spk_hdl, (void*)audio, len);
                    break;
                }
                case AUDIO_ERROR: {
                    ESP_LOGI(TAG, "error");
                    const void *audio = mmap_assets_get_mem(asset_audio, MMAP_AUDIO_ERROR_WAV);
                    uint32_t len = mmap_assets_get_size(asset_audio, MMAP_AUDIO_ERROR_WAV);
                    esp_codec_dev_write(spk_hdl, (void*)audio, len);
                    break;
                }
                default:
                    break;
            }
        }
    }
    vTaskDelete(NULL);
}

static void feed_handler(AppSpeech *self)
{
    esp_afe_sr_data_t *afe_data = self->afe_data;
    int audio_chunksize = self->afe_handle->get_feed_chunksize(afe_data);
    int nch = self->afe_handle->get_channel_num(afe_data);
    int feed_channel = 1;
    assert(nch <= feed_channel);
    int16_t *i2s_buff = (int16_t *)malloc(audio_chunksize * sizeof(int16_t) * 3);
    assert(i2s_buff);

    while (true)
    {
        esp_codec_dev_read(mic_hdl, i2s_buff, audio_chunksize * sizeof(int16_t) * feed_channel);

        // FatfsComboWrite(i2s_buff, audio_chunksize * I2S_CHANNEL_NUM * sizeof(int16_t), 1, fp);
        for (int  i = audio_chunksize - 1; i >= 0; i--) {
            i2s_buff[i * 2 + 1] = 0;
            i2s_buff[i * 2 + 0] = i2s_buff[i];
        }
        self->afe_handle->feed(afe_data, (int16_t *)i2s_buff);
    }
    self->afe_handle->destroy(afe_data);
    if (i2s_buff) {
        free(i2s_buff);
        i2s_buff = NULL;
    }
    vTaskDelete(NULL);
}

static void detect_hander(AppSpeech *self)
{
    esp_afe_sr_data_t *afe_data = self->afe_data;
    int afe_chunksize = self->afe_handle->get_fetch_chunksize(afe_data);
    char *mn_name = esp_srmodel_filter(self->models, ESP_MN_PREFIX, ESP_MN_CHINESE);
    ESP_LOGI(TAG, "multinet:%s\n", mn_name);
    esp_mn_iface_t *multinet = esp_mn_handle_from_name(mn_name);
    model_iface_data_t *model_data = multinet->create(mn_name, 6000);
    esp_mn_commands_update_from_sdkconfig(multinet, model_data); // Add speech commands from sdkconfig
    int mu_chunksize = multinet->get_samp_chunksize(model_data);
    assert(mu_chunksize == afe_chunksize);

    //print active speech commands
    multinet->print_active_speech_commands(model_data);
    ESP_LOGI(TAG, "Ready");

    self->detected = true;

    while (true)
    {
        afe_fetch_result_t* res = self->afe_handle->fetch(afe_data);
        if (!res || res->ret_value == ESP_FAIL) {
            ESP_LOGE(TAG, "fetch error!\n");
            break;
        }

        if (self->detected) {
            esp_mn_state_t mn_state = multinet->detect(model_data, res->data);

            if (mn_state == ESP_MN_STATE_DETECTING) {
                self->command = COMMAND_NOT_DETECTED;
                continue;
            } else if (mn_state == ESP_MN_STATE_DETECTED) {
                esp_mn_results_t *mn_result = multinet->get_results(model_data);
                for (int i = 0; i < mn_result->num; i++) {
                    ESP_LOGI(TAG, "TOP %d, command_id: %d, phrase_id: %d, string:%s prob: %f\n",
                    i+1, mn_result->command_id[i], mn_result->phrase_id[i], mn_result->string, mn_result->prob[i]);
                }
                self->command = (command_word_t)mn_result->command_id[0];
                self->notify();
            } 
        }
    }
    if (model_data) {
        multinet->destroy(model_data);
        model_data = NULL;
    }
    self->afe_handle->destroy(afe_data);
    vTaskDelete(NULL);
}

#define AFE_CONFIG_DEFAULT() { \
    .aec_init = true, \
    .se_init = true, \
    .vad_init = true, \
    .wakenet_init = false, \
    .voice_communication_init = false, \
    .voice_communication_agc_init = false, \
    .voice_communication_agc_gain = 15, \
    .vad_mode = VAD_MODE_3, \
    .wakenet_model_name = NULL, \
    .wakenet_model_name_2 = NULL, \
    .wakenet_mode = DET_MODE_2CH_90, \
    .afe_mode = SR_MODE_LOW_COST, \
    .afe_perferred_core = 0, \
    .afe_perferred_priority = 5, \
    .afe_ringbuf_size = 50, \
    .memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM, \
    .afe_linear_gain = 1.0, \
    .agc_mode = AFE_MN_PEAK_AGC_MODE_2, \
    .pcm_config = { \
        .total_ch_num = 3, \
        .mic_num = 2, \
        .ref_num = 1, \
        .sample_rate = 16000, \
    }, \
    .debug_init = false, \
    .debug_hook = {{AFE_DEBUG_HOOK_MASE_TASK_IN, NULL}, {AFE_DEBUG_HOOK_FETCH_TASK_IN, NULL}}, \
    .afe_ns_mode = NS_MODE_SSP, \
    .afe_ns_model_name = NULL, \
    .fixed_first_channel = true, \
}

AppSpeech::AppSpeech() : afe_handle(&ESP_AFE_SR_HANDLE), detected(false), command(COMMAND_TIMEOUT)
{
    this->models = esp_srmodel_init("model");
    i2s_init(1, 16000, 2, 16);

    afe_config_t afe_config = AFE_CONFIG_DEFAULT();

    afe_config.pcm_config.total_ch_num = 2;
    afe_config.pcm_config.mic_num = 1;
    afe_config.pcm_config.ref_num = 1;

    this->afe_data = this->afe_handle->create_from_config(&afe_config);
}

void AppSpeech::run()
{
    g_queue_audio_play = xQueueCreate(1, sizeof(audio_play_state_t));
    xTaskCreate(audio_play_task, "audio_play_task", 1024 * 4, NULL, 10, NULL);
    xTaskCreatePinnedToCore((TaskFunction_t)feed_handler, "App/SR/Feed", 4 * 1024, this, 5, NULL, 0);
    xTaskCreatePinnedToCore((TaskFunction_t)detect_hander, "App/SR/Detect", 5 * 1024, this, 5, NULL, 1);
    
    audio_play_state_t audio_result = AUDIO_SUCCESS;
    xQueueSend(g_queue_audio_play, &audio_result, 10);
}