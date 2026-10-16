#include "config.h"
#include "can/can_config.h"
/* deep-dog feature gate: whole-file */
#if DEEP_DOG_CAN_ENABLE

#include "ESP32-TWAI-CAN.hpp"

void TwaiCAN::setSpeed(TwaiSpeed twaiSpeed) {
    if(twaiSpeed < TWAI_SPEED_SIZE) speed = twaiSpeed;
}

// clang-format off
uint32_t TwaiCAN::getSpeedNumeric() { 
    uint32_t actualSpeed = 500;
    switch(getSpeed()) {
        default: break;
        #if (SOC_TWAI_BRP_MAX > 256)
        case TWAI_SPEED_1KBPS   :   actualSpeed = 1   ; break;
        case TWAI_SPEED_5KBPS   :   actualSpeed = 5   ; break;
        case TWAI_SPEED_10KBPS  :   actualSpeed = 10  ; break;
        #endif
        #if (SOC_TWAI_BRP_MAX > 128) || (CONFIG_ESP32_REV_MIN_FULL >= 200)
        case TWAI_SPEED_12_5KBPS:   actualSpeed = 12  ; break;
        case TWAI_SPEED_16KBPS  :   actualSpeed = 16  ; break;
        case TWAI_SPEED_20KBPS  :   actualSpeed = 20  ; break;
        #endif
        case TWAI_SPEED_50KBPS  :   actualSpeed = 50  ; break;
        case TWAI_SPEED_100KBPS :   actualSpeed = 100 ; break;
        case TWAI_SPEED_125KBPS :   actualSpeed = 125 ; break;
        case TWAI_SPEED_250KBPS :   actualSpeed = 250 ; break;
        case TWAI_SPEED_500KBPS :   actualSpeed = 500 ; break;
        case TWAI_SPEED_800KBPS :   actualSpeed = 800 ; break;
        case TWAI_SPEED_1000KBPS:   actualSpeed = 1000; break;
    }
    return actualSpeed;
}
// clang-format on

// clang-format off
TwaiSpeed TwaiCAN::convertSpeed(uint16_t canSpeed) { 
    TwaiSpeed actualSpeed = getSpeed();
    switch(canSpeed) {
        default: break;
        #if (SOC_TWAI_BRP_MAX > 256)
        case 1:     actualSpeed = TWAI_SPEED_1KBPS;     break;
        case 5:     actualSpeed = TWAI_SPEED_5KBPS;     break;
        case 10:    actualSpeed = TWAI_SPEED_10KBPS;    break;
        #endif
        #if (SOC_TWAI_BRP_MAX > 128) || (CONFIG_ESP32_REV_MIN_FULL >= 200)
        case 13:    // Just to handle those who would round upwards..
        case 12:    actualSpeed = TWAI_SPEED_12_5KBPS;  break;
        case 16:    actualSpeed = TWAI_SPEED_16KBPS;    break;
        case 20:    actualSpeed = TWAI_SPEED_20KBPS;    break;
        #endif
        case 50:    actualSpeed = TWAI_SPEED_50KBPS;    break;
        case 100:   actualSpeed = TWAI_SPEED_100KBPS;   break;
        case 125:   actualSpeed = TWAI_SPEED_125KBPS;   break;
        case 250:   actualSpeed = TWAI_SPEED_250KBPS;   break;
        case 500:   actualSpeed = TWAI_SPEED_500KBPS;   break;
        case 800:   actualSpeed = TWAI_SPEED_800KBPS;   break;
        case 1000:  actualSpeed = TWAI_SPEED_1000KBPS;  break;
    }
    return actualSpeed;
}
// clang-format on

void TwaiCAN::setTxQueueSize(uint16_t txQueue) {
    if(txQueue != 0xFFFF) txQueueSize = txQueue;
}

void TwaiCAN::setRxQueueSize(uint16_t rxQueue) {
    if(rxQueue != 0xFFFF) rxQueueSize = rxQueue;
}

bool TwaiCAN::getStatusInfo() {
    return ESP_OK == twai_get_status_info(&status);
}

uint32_t TwaiCAN::inTxQueue() {
    uint32_t ret = 0;
    if(getStatusInfo()) {
        ret = status.msgs_to_tx;
    }
    return ret;
};

uint32_t TwaiCAN::inRxQueue() {
    uint32_t ret = 0;
    if(getStatusInfo()) {
        ret = status.msgs_to_rx;
    }
    return ret;
};

uint32_t TwaiCAN::rxErrorCounter() {
    uint32_t ret = 0;
    if(getStatusInfo()) {
        ret = status.rx_error_counter;
    }
    return ret;
};

uint32_t TwaiCAN::txErrorCounter() {
    uint32_t ret = 0;
    if(getStatusInfo()) {
        ret = status.tx_error_counter;
    }
    return ret;
};

uint32_t TwaiCAN::rxMissedCounter() {
    uint32_t ret = 0;
    if(getStatusInfo()) {
        ret = status.rx_missed_count;
    }
    return ret;
};

uint32_t TwaiCAN::txFailedCounter() {
    uint32_t ret = 0;
    if(getStatusInfo()) {
        ret = status.tx_failed_count;
    }
    return ret;
};

uint32_t TwaiCAN::busErrCounter() {
    uint32_t ret = 0;
    if(getStatusInfo()) {
        ret = status.bus_error_count;
    }
    return ret;
};

uint32_t TwaiCAN::canState() {
    uint32_t ret = 0;
    if(getStatusInfo()) {
        ret = (uint32_t)status.state;
    }
    return ret;
};

bool TwaiCAN::setPins(int8_t txPin, int8_t rxPin) {
    bool ret = !init;

    if(txPin >= 0)
        tx = txPin;
    else
        ret = false;
    if(rxPin >= 0)
        rx = rxPin;
    else
        ret = false;

    LOG_TWAI("Wrong pins or CAN bus running already!");
    return ret;
}

bool TwaiCAN::recover(void) {
    if(!getStatusInfo()) {
        LOG_TWAI("CAN bus status read failed!");
        return false;
    }
    switch(status.state) {
        case TWAI_STATE_BUS_OFF: {
            LOG_TWAI("Bus was off, starting recovery");
            return twai_initiate_recovery();
        }
        case TWAI_STATE_RECOVERING: {
            // Already recovering, nothing to do
            return true;
        }
        case TWAI_STATE_STOPPED: {
            // Stopped, nothing to do
            return true;
        }
        default: {
            LOG_TWAI("Wrong state for recovery!");
        }
    }

    return false;
}

bool TwaiCAN::restart(void) {
    if(!getStatusInfo()) {
        LOG_TWAI("CAN bus status read failed!");
        return false;
    }
    switch(status.state) {
        case TWAI_STATE_STOPPED: {
            // Stopped, restart
            return twai_start();
        }
        default: {
            LOG_TWAI("Wrong state for restart!");
        }
    }

    return false;
}

bool TwaiCAN::begin(TwaiSpeed              twaiSpeed,
                    int8_t                 txPin,
                    int8_t                 rxPin,
                    uint16_t               txQueue,
                    uint16_t               rxQueue,
                    twai_filter_config_t*  fConfig,
                    twai_general_config_t* gConfig,
                    twai_timing_config_t*  tConfig) {
    bool ret = false;
    if(end()) {
        init = true;
        setSpeed(twaiSpeed);
        setPins(txPin, rxPin);

        gpio_reset_pin((gpio_num_t)rx);
        gpio_reset_pin((gpio_num_t)tx);

        setTxQueueSize(txQueue);
        setRxQueueSize(rxQueue);

        twai_general_config_t g_config = {.controller_id   = 0,
                                          .mode           = TWAI_MODE_NORMAL,
                                          .tx_io          = (gpio_num_t)tx,
                                          .rx_io          = (gpio_num_t)rx,
                                          .clkout_io      = TWAI_IO_UNUSED,
                                          .bus_off_io     = TWAI_IO_UNUSED,
                                          .tx_queue_len   = txQueueSize,
                                          .rx_queue_len   = rxQueueSize,
                                          .alerts_enabled = TWAI_ALERT_NONE,
                                          .clkout_divider = 0,
                                    #if CONFIG_TWAI_ISR_IN_IRAM
                                          .intr_flags     = ESP_INTR_FLAG_IRAM,
                                    #else
                                          .intr_flags     = ESP_INTR_FLAG_LEVEL1,
                                    #endif
                                          .general_flags  = {0}};
#if DEEP_DOG_CAN_LISTEN_ONLY
        // [deep-dog] 诊断：只听模式（不发 ACK / 不发 error flag），用于隔离「本节点是否干扰总线」
        g_config.mode = TWAI_MODE_LISTEN_ONLY;
#endif
        // clang-format off
        twai_timing_config_t t_config[TWAI_SPEED_SIZE] = {
            #if (SOC_TWAI_BRP_MAX > 256)
            TWAI_TIMING_CONFIG_1KBITS(),
            TWAI_TIMING_CONFIG_5KBITS(),
            TWAI_TIMING_CONFIG_10KBITS(),
            #endif
            #if (SOC_TWAI_BRP_MAX > 128) || (CONFIG_ESP32_REV_MIN_FULL >= 200)
            TWAI_TIMING_CONFIG_12_5KBITS(),
            TWAI_TIMING_CONFIG_16KBITS(),
            TWAI_TIMING_CONFIG_20KBITS(),
            #endif
            TWAI_TIMING_CONFIG_50KBITS(),
            TWAI_TIMING_CONFIG_100KBITS(),
            TWAI_TIMING_CONFIG_125KBITS(),
            TWAI_TIMING_CONFIG_250KBITS(),
            TWAI_TIMING_CONFIG_500KBITS(),
            TWAI_TIMING_CONFIG_800KBITS(),
            TWAI_TIMING_CONFIG_1MBITS()
        };
        // clang-format on

        twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

#if DEEP_DOG_CAN_TIMING_875
        // [deep-dog] 1 Mbps 改用 **87.5% 采样点**（8 Tq：tseg_1=6, tseg_2=1），与 CiA 建议、
        // PCAN 等多数对端默认一致。IDF 默认的 1MBITS 是 75%（tseg_1=5,tseg_2=2）；
        // 采样点与总线其余节点不一致时，典型症状是「**能收到、发不出去**」——
        // 发送时在本节点采样点读 ACK 槽读成隐性 → ACK 错误 → TEC +8 → 顶到 256 → Bus-Off。
        twai_timing_config_t t_1m_875 = {
            .clk_src = TWAI_CLK_SRC_DEFAULT,
            .quanta_resolution_hz = 8000000,  // Tq = 125ns
            .brp = 0,
            .prop_seg = 0,
            .tseg_1 = 6,   // 采样点 = (1+6)/8 = 87.5%
            .tseg_2 = 1,   // 1+6+1 = 8 Tq = 1µs @1Mbps
            .sjw = 1,
            .ssp_offset = 0,
            .triple_sampling = false,
        };
#endif

        if(!gConfig) gConfig = &g_config;
        if(!fConfig) fConfig = &f_config;
        if(!tConfig) {
            tConfig = &t_config[speed];
#if DEEP_DOG_CAN_TIMING_875
            if(speed == TWAI_SPEED_1000KBPS) {
                tConfig = &t_1m_875;
            }
#endif
        }

        // Install TWAI driver
        if(twai_driver_install(gConfig, tConfig, fConfig) == ESP_OK) {
            LOG_TWAI("Driver installed");
        } else {
            LOG_TWAI("Failed to install driver");
        }

        // Start TWAI driver
        if(twai_start() == ESP_OK) {
            LOG_TWAI("Driver started");
            ret = true;
        } else {
            LOG_TWAI("Failed to start driver");
        }
        if(!ret) end();
    }
    return ret;
}

bool TwaiCAN::end() {
    bool ret = false;
    if(init) {
        // Stop the TWAI driver
        if(twai_stop() == ESP_OK) {
            LOG_TWAI("Driver stopped\n");
            ret = true;
        } else {
            LOG_TWAI("Failed to stop driver\n");
        }

        // Uninstall the TWAI driver
        if(twai_driver_uninstall() == ESP_OK) {
            LOG_TWAI("Driver uninstalled\n");
            ret &= true;
        } else {
            LOG_TWAI("Failed to uninstall driver\n");
            ret &= false;
        }
        init = !ret;
    } else
        ret = true;
    return ret;
}

TwaiCAN ESP32Can;

#endif  // DEEP_DOG_CAN_ENABLE
