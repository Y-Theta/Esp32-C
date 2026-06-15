#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_spiffs.h"

#include "llcc68.h"
#include "app_state.h"
#include "web_server.h"

static const char *TAG = "APP";

/* ---------------- Wi-Fi AP 配置 ---------------- */
#define WIFI_AP_SSID      "LLCC68_Config"
#define WIFI_AP_PASSWORD  "12345678"
#define WIFI_AP_CHANNEL   6
#define WIFI_AP_MAX_CONN  4

/* ---------------- 全局 LoRa 对象 ---------------- */
static LLCC68 radio;

/* ---------------- SPIFFS 初始化 ---------------- */
static esp_err_t spiffs_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = nullptr,
        .max_files = 5,
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_vfs_spiffs_register failed: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0;
    size_t used = 0;

    ret = esp_spiffs_info(nullptr, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_spiffs_info failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(
        TAG,
        "SPIFFS mounted: total=%u, used=%u",
        static_cast<unsigned>(total),
        static_cast<unsigned>(used)
    );

    return ESP_OK;
}

/* ---------------- Wi-Fi 事件 ---------------- */
static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        auto *event = static_cast<wifi_event_ap_staconnected_t *>(event_data);
        ESP_LOGI(TAG, "station connected, AID=%d", event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        auto *event = static_cast<wifi_event_ap_stadisconnected_t *>(event_data);
        ESP_LOGI(TAG, "station disconnected, AID=%d", event->aid);
    }
}

/* ---------------- Wi-Fi AP 初始化 ---------------- */
static esp_err_t wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();

    if (!ap_netif) {
        ESP_LOGE(TAG, "esp_netif_create_default_wifi_ap failed");
        return ESP_FAIL;
    }

    /*
     * 设置 AP IP 为 172.16.0.1
     */
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(ap_netif));

    esp_netif_ip_info_t ip_info = {};
    ip_info.ip.addr = ESP_IP4TOADDR(172, 16, 0, 1);
    ip_info.gw.addr = ESP_IP4TOADDR(172, 16, 0, 1);
    ip_info.netmask.addr = ESP_IP4TOADDR(255, 255, 255, 0);

    ESP_ERROR_CHECK(esp_netif_set_ip_info(ap_netif, &ip_info));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(ap_netif));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &wifi_event_handler,
            nullptr,
            nullptr
        )
    );

    wifi_config_t wifi_config = {};

    strncpy(
        reinterpret_cast<char *>(wifi_config.ap.ssid),
        WIFI_AP_SSID,
        sizeof(wifi_config.ap.ssid)
    );

    wifi_config.ap.ssid_len = strlen(WIFI_AP_SSID);

    strncpy(
        reinterpret_cast<char *>(wifi_config.ap.password),
        WIFI_AP_PASSWORD,
        sizeof(wifi_config.ap.password)
    );

    wifi_config.ap.channel = WIFI_AP_CHANNEL;
    wifi_config.ap.max_connection = WIFI_AP_MAX_CONN;
    wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;

    if (strlen(WIFI_AP_PASSWORD) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP started");
    ESP_LOGI(TAG, "SSID: %s", WIFI_AP_SSID);
    ESP_LOGI(TAG, "PASSWORD: %s", WIFI_AP_PASSWORD);
    ESP_LOGI(TAG, "URL: http://172.16.0.1");

    return ESP_OK;
}

/* ---------------- LoRa 发送任务 ---------------- */
static void lora_tx_task(void *arg)
{
    ESP_LOGI(TAG, "LoRa TX task started on core %d", xPortGetCoreID());

    AppState& state = AppState::instance();

    ESP_ERROR_CHECK(radio.init());

    {
        LLCC68::Config cfg = state.getConfig();

        xSemaphoreTake(state.radioMutex(), portMAX_DELAY);
        ESP_ERROR_CHECK(radio.applyConfig(cfg));
        xSemaphoreGive(state.radioMutex());
    }

    uint32_t counter = 0;

    while (1) {
        LLCC68::Config cfg = state.getConfig();
        bool needApply = state.takeNeedApply();

        if (needApply) {
            ESP_LOGI(TAG, "Applying new LoRa config");

            xSemaphoreTake(state.radioMutex(), portMAX_DELAY);
            esp_err_t err = radio.applyConfig(cfg);
            xSemaphoreGive(state.radioMutex());

            if (err != ESP_OK) {
                ESP_LOGE(TAG, "radio.applyConfig failed: %s", esp_err_to_name(err));
            } else {
                ESP_LOGI(TAG, "LoRa config applied");
            }
        }

        char payload[96];

        int len = snprintf(
            payload,
            sizeof(payload),
            "LLCC68 TX #%lu F=%lu SF=%u",
            static_cast<unsigned long>(counter++),
            static_cast<unsigned long>(cfg.freqHz),
            cfg.spreadingFactor
        );

        xSemaphoreTake(state.radioMutex(), portMAX_DELAY);

        esp_err_t err = radio.setPacketParams(static_cast<uint8_t>(len));

        if (err == ESP_OK) {
            err = radio.writeBuffer(
                reinterpret_cast<const uint8_t *>(payload),
                static_cast<uint8_t>(len)
            );
        }

        if (err == ESP_OK) {
            err = radio.clearIrq(0xFFFF);
        }

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "TX => %s", payload);
            err = radio.setTx(0x000000);
        }

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "TX prepare failed: %s", esp_err_to_name(err));
            xSemaphoreGive(state.radioMutex());
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        uint32_t wait_ms = 0;

        while (!radio.isDio1High()) {
            vTaskDelay(pdMS_TO_TICKS(10));
            wait_ms += 10;

            if (wait_ms >= 5000) {
                ESP_LOGW(TAG, "wait DIO1 timeout");
                break;
            }
        }

        uint16_t irq = 0;

        err = radio.getIrqStatus(&irq);

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "IRQ = 0x%04X", irq);

            radio.clearIrq(irq);

            if (irq & LLCC68::IRQ_TX_DONE) {
                ESP_LOGI(TAG, "TX done");
            }

            if (irq & LLCC68::IRQ_TIMEOUT) {
                ESP_LOGW(TAG, "TX timeout");
            }
        } else {
            ESP_LOGE(TAG, "getIrqStatus failed: %s", esp_err_to_name(err));
        }

        xSemaphoreGive(state.radioMutex());

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/* ---------------- app_main ---------------- */
extern "C" void app_main(void)
{
    esp_err_t err;

    AppState& state = AppState::instance();

    if (!state.init()) {
        ESP_LOGE(TAG, "AppState init failed");
        return;
    }

    /*
     * NVS
     */
    err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }

    /*
     * SPIFFS
     */
    ESP_ERROR_CHECK(spiffs_init());

    /*
     * Wi-Fi AP
     */
    ESP_ERROR_CHECK(wifi_init_softap());

    /*
     * Web Server
     *
     * 注意：
     * 这里用 static，避免 app_main 结束后 WebServer 对象析构。
     */
    static WebServer webServer;
    ESP_ERROR_CHECK(webServer.start());

    /*
     * LoRa 任务放 CPU1
     */
    BaseType_t ret = xTaskCreatePinnedToCore(
        lora_tx_task,
        "lora_tx_task",
        4096,
        nullptr,
        5,
        nullptr,
        1
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "create lora_tx_task failed");
        return;
    }

    ESP_LOGI(TAG, "app_main done");
}