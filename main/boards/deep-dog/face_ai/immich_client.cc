/**
 * Immich 真名（S05）：esp_http_client 上传裁剪脸 → 轮询 people；可选删临时 asset。
 * 不主动 PUT /jobs（由 Immich 上传入队即可）。
 */
#include "sdkconfig.h"

#include "immich_client.h"
#include "face_recognize.h"
#include "face_ai_bridge.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#include <cJSON.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <nvs.h>
#include <nvs_flash.h>

#if DEEP_DOG_FACE_AI_ENABLE && DEEP_DOG_FACE_IMMICH_ENABLE && defined(CONFIG_IDF_TARGET_ESP32S3)

#define TAG "dog_immich"

struct ImmichJob {
    int local_id = 0;
    uint8_t* jpeg = nullptr;
    size_t jpeg_len = 0;
    bool force = false;
};

static QueueHandle_t s_queue = nullptr;
static TaskHandle_t s_task = nullptr;
static bool s_started = false;

static char s_api_url[96] = DEEP_DOG_FACE_IMMICH_DEFAULT_URL;
static char s_api_key[96] = {};
static uint8_t s_delete_asset = DEEP_DOG_FACE_IMMICH_DELETE_ASSET ? 1 : 0;
static std::atomic<int> s_force_refresh_id{0};
static std::atomic<int> s_inflight_id{0};
static int64_t s_backoff_until_us[DEEP_DOG_FACE_RECOG_MAX + 1]{};
static char s_last_result[64] = "idle";
static int s_last_local_id = 0;

static void SetLastResult(const char* r, int local_id) {
    snprintf(s_last_result, sizeof(s_last_result), "%s", r ? r : "");
    s_last_local_id = local_id;
}

static void LoadConfigFromNvs() {
    nvs_handle_t h;
    if (nvs_open(DEEP_DOG_FACE_IMMICH_NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t sz = sizeof(s_api_url);
    if (nvs_get_str(h, "api_url", s_api_url, &sz) != ESP_OK || s_api_url[0] == '\0') {
        strncpy(s_api_url, DEEP_DOG_FACE_IMMICH_DEFAULT_URL, sizeof(s_api_url) - 1);
    }
    sz = sizeof(s_api_key);
    (void)nvs_get_str(h, "api_key", s_api_key, &sz);
    uint8_t del = DEEP_DOG_FACE_IMMICH_DELETE_ASSET ? 1 : 0;
    if (nvs_get_u8(h, "del_asset", &del) == ESP_OK) {
        s_delete_asset = del ? 1 : 0;
    } else {
        s_delete_asset = DEEP_DOG_FACE_IMMICH_DELETE_ASSET ? 1 : 0;
    }
    nvs_close(h);
}

static bool SaveConfigToNvs() {
    nvs_handle_t h;
    if (nvs_open(DEEP_DOG_FACE_IMMICH_NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    esp_err_t err = nvs_set_str(h, "api_url", s_api_url);
    if (err == ESP_OK) {
        err = nvs_set_str(h, "api_key", s_api_key);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(h, "del_asset", s_delete_asset ? 1 : 0);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err == ESP_OK;
}

static void TrimTrailingSlash(char* url) {
    size_t n = strlen(url);
    while (n > 0 && url[n - 1] == '/') {
        url[--n] = '\0';
    }
}

struct HttpBuf {
    char* data = nullptr;
    size_t len = 0;
    size_t cap = 0;
};

static esp_err_t HttpEvent(esp_http_client_event_t* evt) {
    if (evt->event_id != HTTP_EVENT_ON_DATA || !evt->user_data || !evt->data || evt->data_len <= 0) {
        return ESP_OK;
    }
    auto* buf = static_cast<HttpBuf*>(evt->user_data);
    const size_t need = buf->len + (size_t)evt->data_len + 1;
    if (need > buf->cap) {
        size_t ncap = buf->cap ? buf->cap * 2 : 1024;
        while (ncap < need) {
            ncap *= 2;
        }
        char* nd = (char*)heap_caps_realloc(buf->data, ncap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!nd) {
            nd = (char*)heap_caps_realloc(buf->data, ncap, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (!nd) {
            return ESP_ERR_NO_MEM;
        }
        buf->data = nd;
        buf->cap = ncap;
    }
    memcpy(buf->data + buf->len, evt->data, (size_t)evt->data_len);
    buf->len += (size_t)evt->data_len;
    buf->data[buf->len] = '\0';
    return ESP_OK;
}

static int HttpDo(esp_http_client_method_t method, const char* url, const char* content_type,
                  const uint8_t* body, size_t body_len, HttpBuf* out) {
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.method = method;
    cfg.timeout_ms = 30000;
    cfg.event_handler = HttpEvent;
    cfg.user_data = out;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return -1;
    }
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "x-api-key", s_api_key);
    if (content_type) {
        esp_http_client_set_header(client, "Content-Type", content_type);
    }
    if (body && body_len > 0) {
        esp_http_client_set_post_field(client, (const char*)body, (int)body_len);
    }
    esp_err_t err = esp_http_client_perform(client);
    int status = -1;
    if (err == ESP_OK) {
        status = esp_http_client_get_status_code(client);
    } else {
        ESP_LOGW(TAG, "http %s failed: %s", url, esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
    return status;
}

static bool ParsePeopleName(const char* json, char* name_out, size_t name_sz, char* pid_out, size_t pid_sz) {
    if (!json || !name_out || name_sz == 0) {
        return false;
    }
    name_out[0] = '\0';
    if (pid_out && pid_sz) {
        pid_out[0] = '\0';
    }
    cJSON* root = cJSON_Parse(json);
    if (!root) {
        return false;
    }
    bool ok = false;
    cJSON* people = cJSON_GetObjectItem(root, "people");
    if (cJSON_IsArray(people)) {
        const int n = cJSON_GetArraySize(people);
        for (int i = 0; i < n; i++) {
            cJSON* p = cJSON_GetArrayItem(people, i);
            cJSON* name = cJSON_GetObjectItem(p, "name");
            if (cJSON_IsString(name) && name->valuestring && name->valuestring[0] &&
                strcmp(name->valuestring, "未命名") != 0) {
                strncpy(name_out, name->valuestring, name_sz - 1);
                name_out[name_sz - 1] = '\0';
                cJSON* id = cJSON_GetObjectItem(p, "id");
                if (pid_out && pid_sz && cJSON_IsString(id) && id->valuestring) {
                    strncpy(pid_out, id->valuestring, pid_sz - 1);
                    pid_out[pid_sz - 1] = '\0';
                }
                ok = true;
                break;
            }
        }
    }
    cJSON_Delete(root);
    return ok;
}

static bool UploadAsset(const uint8_t* jpeg, size_t jpeg_len, char* asset_id_out, size_t asset_id_sz) {
    if (!jpeg || jpeg_len == 0 || !asset_id_out || asset_id_sz == 0) {
        return false;
    }
    asset_id_out[0] = '\0';

    char boundary[48];
    snprintf(boundary, sizeof(boundary), "----dog%d", (int)(esp_random() & 0x7fffffff));

    char device_asset_id[40];
    snprintf(device_asset_id, sizeof(device_asset_id), "dog-%08x-%08x", (unsigned)esp_random(),
             (unsigned)(esp_timer_get_time() & 0xffffffffu));

    char meta[512];
    int meta_len = snprintf(
        meta, sizeof(meta),
        "--%s\r\nContent-Disposition: form-data; name=\"deviceAssetId\"\r\n\r\n%s\r\n"
        "--%s\r\nContent-Disposition: form-data; name=\"deviceId\"\r\n\r\ndeep-dog\r\n"
        "--%s\r\nContent-Disposition: form-data; name=\"fileCreatedAt\"\r\n\r\n2026-01-01T00:00:00.000Z\r\n"
        "--%s\r\nContent-Disposition: form-data; name=\"fileModifiedAt\"\r\n\r\n2026-01-01T00:00:00.000Z\r\n"
        "--%s\r\nContent-Disposition: form-data; name=\"assetData\"; filename=\"face.jpg\"\r\n"
        "Content-Type: image/jpeg\r\n\r\n",
        boundary, device_asset_id, boundary, boundary, boundary, boundary);
    if (meta_len <= 0 || (size_t)meta_len >= sizeof(meta)) {
        return false;
    }
    char tail[64];
    int tail_len = snprintf(tail, sizeof(tail), "\r\n--%s--\r\n", boundary);
    if (tail_len <= 0) {
        return false;
    }

    const size_t total = (size_t)meta_len + jpeg_len + (size_t)tail_len;
    uint8_t* body = (uint8_t*)heap_caps_malloc(total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!body) {
        body = (uint8_t*)heap_caps_malloc(total, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!body) {
        return false;
    }
    memcpy(body, meta, (size_t)meta_len);
    memcpy(body + meta_len, jpeg, jpeg_len);
    memcpy(body + meta_len + jpeg_len, tail, (size_t)tail_len);

    char url[128];
    snprintf(url, sizeof(url), "%s/assets", s_api_url);
    char ctype[80];
    snprintf(ctype, sizeof(ctype), "multipart/form-data; boundary=%s", boundary);

    HttpBuf resp{};
    const int status = HttpDo(HTTP_METHOD_POST, url, ctype, body, total, &resp);
    heap_caps_free(body);

    bool ok = false;
    if ((status == 200 || status == 201) && resp.data) {
        cJSON* root = cJSON_Parse(resp.data);
        if (root) {
            cJSON* id = cJSON_GetObjectItem(root, "id");
            if (cJSON_IsString(id) && id->valuestring) {
                strncpy(asset_id_out, id->valuestring, asset_id_sz - 1);
                asset_id_out[asset_id_sz - 1] = '\0';
                cJSON* st = cJSON_GetObjectItem(root, "status");
                ESP_LOGI(TAG, "upload ok asset=%s status=%s", asset_id_out,
                         cJSON_IsString(st) ? st->valuestring : "?");
                ok = true;
            }
            cJSON_Delete(root);
        }
    } else {
        ESP_LOGW(TAG, "upload http=%d body=%s", status, resp.data ? resp.data : "");
    }
    heap_caps_free(resp.data);
    return ok;
}

static bool PollPerson(const char* asset_id, char* name_out, size_t name_sz, char* pid_out, size_t pid_sz) {
    char url[160];
    snprintf(url, sizeof(url), "%s/assets/%s", s_api_url, asset_id);
    for (int i = 0; i < DEEP_DOG_FACE_IMMICH_POLL_MAX; i++) {
        HttpBuf resp{};
        const int status = HttpDo(HTTP_METHOD_GET, url, nullptr, nullptr, 0, &resp);
        if (status == 200 && resp.data && ParsePeopleName(resp.data, name_out, name_sz, pid_out, pid_sz)) {
            heap_caps_free(resp.data);
            ESP_LOGI(TAG, "poll#%d matched name=%s", i, name_out);
            return true;
        }
        heap_caps_free(resp.data);
        if (i + 1 < DEEP_DOG_FACE_IMMICH_POLL_MAX) {
            vTaskDelay(pdMS_TO_TICKS(DEEP_DOG_FACE_IMMICH_POLL_MS));
        }
    }
    ESP_LOGW(TAG, "poll timeout asset=%s", asset_id);
    return false;
}

static void DeleteAsset(const char* asset_id) {
    if (!asset_id || !asset_id[0]) {
        return;
    }
    char url[128];
    snprintf(url, sizeof(url), "%s/assets", s_api_url);
    char body[96];
    snprintf(body, sizeof(body), "{\"ids\":[\"%s\"]}", asset_id);
    HttpBuf resp{};
    const int status = HttpDo(HTTP_METHOD_DELETE, url, "application/json", (const uint8_t*)body, strlen(body), &resp);
    ESP_LOGI(TAG, "delete asset=%s http=%d", asset_id, status);
    heap_caps_free(resp.data);
}

static bool InBackoff(int local_id) {
    if (local_id <= 0 || local_id > DEEP_DOG_FACE_RECOG_MAX) {
        return false;
    }
    return esp_timer_get_time() < s_backoff_until_us[local_id];
}

static void SetBackoff(int local_id) {
    if (local_id <= 0 || local_id > DEEP_DOG_FACE_RECOG_MAX) {
        return;
    }
    s_backoff_until_us[local_id] =
        esp_timer_get_time() + (int64_t)DEEP_DOG_FACE_IMMICH_BACKOFF_S * 1000000LL;
}

static void ProcessJob(ImmichJob job) {
    s_inflight_id.store(job.local_id, std::memory_order_relaxed);
    ESP_LOGI(TAG, "naming local_id=%d jpeg=%u force=%d", job.local_id, (unsigned)job.jpeg_len, (int)job.force);

    if (!DeepDogImmichIsConfigured()) {
        SetLastResult("no_key", job.local_id);
        goto done;
    }
    if (!job.force && !DeepDogFaceRecognizeNeedsImmichName(job.local_id)) {
        SetLastResult("has_name", job.local_id);
        goto done;
    }

    {
        char asset_id[48] = {};
        if (!UploadAsset(job.jpeg, job.jpeg_len, asset_id, sizeof(asset_id))) {
            SetLastResult("upload_fail", job.local_id);
            SetBackoff(job.local_id);
            goto done;
        }

        char name[32] = {};
        char person_id[40] = {};
        const bool matched = PollPerson(asset_id, name, sizeof(name), person_id, sizeof(person_id));
        if (s_delete_asset) {
            DeleteAsset(asset_id);
        } else {
            ESP_LOGI(TAG, "keep asset=%s (delete_asset=0)", asset_id);
        }

        if (matched && name[0]) {
            if (DeepDogFaceRecognizeBindImmichName(job.local_id, name, person_id)) {
                DeepDogFaceAiOnImmichName(job.local_id, name);
                SetLastResult("matched", job.local_id);
                ESP_LOGI(TAG, "bound local_id=%d -> %s", job.local_id, name);
            } else {
                SetLastResult("bind_fail", job.local_id);
            }
        } else {
            SetLastResult("unknown", job.local_id);
            SetBackoff(job.local_id);
        }
    }

done:
    if (job.jpeg) {
        heap_caps_free(job.jpeg);
    }
    s_inflight_id.store(0, std::memory_order_relaxed);
}

static void ImmichTask(void* /*arg*/) {
    ImmichJob job{};
    for (;;) {
        if (xQueueReceive(s_queue, &job, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        ProcessJob(job);
    }
}

bool DeepDogImmichInit() {
    if (s_started) {
        return true;
    }
    LoadConfigFromNvs();
    TrimTrailingSlash(s_api_url);
    s_queue = xQueueCreate(1, sizeof(ImmichJob));
    if (!s_queue) {
        return false;
    }
    if (xTaskCreate(ImmichTask, "dog_immich", 12288, nullptr, 2, &s_task) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = nullptr;
        return false;
    }
    s_started = true;
    ESP_LOGI(TAG, "immich worker ready configured=%d url=%s delete_asset=%u",
             (int)DeepDogImmichIsConfigured(), s_api_url, (unsigned)s_delete_asset);
    return true;
}

void DeepDogImmichDeinit() {
    if (!s_started) {
        return;
    }
    if (s_task) {
        vTaskDelete(s_task);
        s_task = nullptr;
    }
    if (s_queue) {
        ImmichJob j{};
        while (xQueueReceive(s_queue, &j, 0) == pdTRUE) {
            if (j.jpeg) {
                heap_caps_free(j.jpeg);
            }
        }
        vQueueDelete(s_queue);
        s_queue = nullptr;
    }
    s_started = false;
}

bool DeepDogImmichIsConfigured() {
    return s_api_key[0] != '\0';
}

bool DeepDogImmichSetConfig(const char* api_url, const char* api_key, int delete_asset) {
    const bool has_key = api_key && api_key[0];
    if (!has_key && !DeepDogImmichIsConfigured()) {
        return false;
    }
    if (has_key) {
        if (api_url && api_url[0]) {
            strncpy(s_api_url, api_url, sizeof(s_api_url) - 1);
            s_api_url[sizeof(s_api_url) - 1] = '\0';
            TrimTrailingSlash(s_api_url);
        } else if (!s_api_url[0]) {
            strncpy(s_api_url, DEEP_DOG_FACE_IMMICH_DEFAULT_URL, sizeof(s_api_url) - 1);
        }
        strncpy(s_api_key, api_key, sizeof(s_api_key) - 1);
        s_api_key[sizeof(s_api_key) - 1] = '\0';
    } else if (api_url && api_url[0]) {
        strncpy(s_api_url, api_url, sizeof(s_api_url) - 1);
        s_api_url[sizeof(s_api_url) - 1] = '\0';
        TrimTrailingSlash(s_api_url);
    }
    if (delete_asset == 0 || delete_asset == 1) {
        s_delete_asset = (uint8_t)delete_asset;
    }
    const bool ok = SaveConfigToNvs();
    ESP_LOGI(TAG, "config saved ok=%d url=%s key_len=%u delete_asset=%u", (int)ok, s_api_url,
             (unsigned)strlen(s_api_key), (unsigned)s_delete_asset);
    return ok;
}

size_t DeepDogImmichFormatStatusJson(char* buf, size_t buf_size) {
    if (!buf || buf_size < 32) {
        return 0;
    }
    return (size_t)snprintf(
        buf, buf_size,
        "{\"configured\":%s,\"url\":\"%s\",\"key_len\":%u,\"delete_asset\":%u,\"inflight\":%d,\"last\":\"%s\","
        "\"last_local_id\":%d}",
        DeepDogImmichIsConfigured() ? "true" : "false", s_api_url, (unsigned)strlen(s_api_key),
        (unsigned)s_delete_asset, s_inflight_id.load(std::memory_order_relaxed), s_last_result, s_last_local_id);
}

bool DeepDogImmichRequestName(int local_id, uint8_t* jpeg, size_t jpeg_len, bool force) {
    if (!jpeg) {
        return false;
    }
    auto free_jpeg = [&]() {
        heap_caps_free(jpeg);
    };
    if (!s_started || !s_queue || local_id <= 0 || jpeg_len == 0) {
        free_jpeg();
        return false;
    }
    if (!DeepDogImmichIsConfigured()) {
        free_jpeg();
        return false;
    }
    if (s_inflight_id.load(std::memory_order_relaxed) == local_id) {
        free_jpeg();
        return false;
    }
    if (!force && InBackoff(local_id)) {
        free_jpeg();
        return false;
    }
    if (!force && !DeepDogFaceRecognizeNeedsImmichName(local_id)) {
        free_jpeg();
        return false;
    }

    ImmichJob job{};
    job.local_id = local_id;
    job.jpeg = jpeg;
    job.jpeg_len = jpeg_len;
    job.force = force;
    if (xQueueSend(s_queue, &job, 0) != pdTRUE) {
        free_jpeg();
        return false;
    }
    ESP_LOGI(TAG, "queued naming local_id=%d len=%u force=%d", local_id, (unsigned)jpeg_len, (int)force);
    return true;
}

void DeepDogImmichRequestRefresh(int local_id) {
    s_force_refresh_id.store(local_id > 0 ? local_id : -1, std::memory_order_relaxed);
    ESP_LOGI(TAG, "force refresh requested local_id=%d", local_id);
}

bool DeepDogImmichConsumeForceRefresh(int local_id) {
    const int want = s_force_refresh_id.load(std::memory_order_relaxed);
    if (want == 0) {
        return false;
    }
    if (want == -1 || want == local_id) {
        s_force_refresh_id.store(0, std::memory_order_relaxed);
        return true;
    }
    return false;
}

#else  // stub

bool DeepDogImmichInit() {
    return true;
}
void DeepDogImmichDeinit() {}
bool DeepDogImmichIsConfigured() {
    return false;
}
bool DeepDogImmichSetConfig(const char*, const char*, int) {
    return false;
}
size_t DeepDogImmichFormatStatusJson(char* buf, size_t buf_size) {
    if (!buf || buf_size == 0) {
        return 0;
    }
    const char* s = "{\"configured\":false,\"enabled\":false}";
    size_t i = 0;
    for (; s[i] && i + 1 < buf_size; i++) {
        buf[i] = s[i];
    }
    buf[i] = '\0';
    return i;
}
bool DeepDogImmichRequestName(int, uint8_t* jpeg, size_t, bool) {
    if (jpeg) {
        heap_caps_free(jpeg);
    }
    return false;
}
void DeepDogImmichRequestRefresh(int) {}
bool DeepDogImmichConsumeForceRefresh(int) {
    return false;
}

#endif
