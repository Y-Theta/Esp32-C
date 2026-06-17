#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <string>

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
#define WIFI_AP_SSID "LLCC68_Config"
#define WIFI_AP_PASSWORD "12345678"
#define WIFI_AP_CHANNEL 6
#define WIFI_AP_MAX_CONN 4

/* ---------------- 全局 LoRa 对象 ---------------- */
static LLCC68 radio;

/* ---------------- SPIFFS 初始化 ---------------- */
static esp_err_t spiffs_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = nullptr,
        .max_files = 5,
        .format_if_mount_failed = true};

    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_vfs_spiffs_register failed: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0;
    size_t used = 0;

    ret = esp_spiffs_info(nullptr, &total, &used);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_spiffs_info failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SPIFFS mounted: total=%u, used=%u",
             static_cast<unsigned>(total),
             static_cast<unsigned>(used));

    return ESP_OK;
}

/* ---------------- Wi-Fi 事件 ---------------- */
static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        auto *event = static_cast<wifi_event_ap_staconnected_t *>(event_data);
        ESP_LOGI(TAG, "station connected, AID=%d", event->aid);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
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

    if (!ap_netif)
    {
        ESP_LOGE(TAG, "esp_netif_create_default_wifi_ap failed");
        return ESP_FAIL;
    }

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
            nullptr));

    wifi_config_t wifi_config = {};

    strncpy(
        reinterpret_cast<char *>(wifi_config.ap.ssid),
        WIFI_AP_SSID,
        sizeof(wifi_config.ap.ssid));
    wifi_config.ap.ssid_len = strlen(WIFI_AP_SSID);

    strncpy(
        reinterpret_cast<char *>(wifi_config.ap.password),
        WIFI_AP_PASSWORD,
        sizeof(wifi_config.ap.password));

    wifi_config.ap.channel = WIFI_AP_CHANNEL;
    wifi_config.ap.max_connection = WIFI_AP_MAX_CONN;
    wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;

    if (strlen(WIFI_AP_PASSWORD) == 0)
    {
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

/*
 * 统一的 LoRa 任务：
 * - 发送模式：从发送队列取消息并发送
 * - 接收模式：监听收到的数据，放入接收缓存
 * - 配置变化：动态重新应用
 */
static void lora_task(void *arg)
{
    ESP_LOGI(TAG, "LoRa task started on core %d", xPortGetCoreID());

    AppState &state = AppState::instance();

    ESP_ERROR_CHECK(radio.init());

    {
        LLCC68::Config cfg = state.getConfig();

        xSemaphoreTake(state.radioMutex(), portMAX_DELAY);
        ESP_ERROR_CHECK(radio.applyConfig(cfg));
        xSemaphoreGive(state.radioMutex());
    }

    while (1)
    {
        LLCC68::Config cfg = state.getConfig();

        /*
         * 如果有配置更新，重新应用
         */
        if (state.takeNeedApply())
        {
            ESP_LOGI(TAG, "Applying new LoRa config");

            xSemaphoreTake(state.radioMutex(), portMAX_DELAY);
            esp_err_t err = radio.applyConfig(cfg);
            xSemaphoreGive(state.radioMutex());

            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "radio.applyConfig failed: %s", esp_err_to_name(err));
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }

            ESP_LOGI(TAG, "LoRa config applied");
        }

        /*
         * 发送模式
         */
        if (state.isLoraTxMode())
        {
            std::string txMsg;

            /*
             * 取待发送消息
             * 如果没有消息，就简单延时，不刷屏
             */
            if (!state.popTxMessage(txMsg))
            {
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            if (txMsg.empty())
            {
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            xSemaphoreTake(state.radioMutex(), portMAX_DELAY);

            esp_err_t err = radio.setPacketParams(static_cast<uint8_t>(txMsg.length()));

            if (err == ESP_OK)
            {
                err = radio.writeBuffer(
                    reinterpret_cast<const uint8_t *>(txMsg.data()),
                    static_cast<uint8_t>(txMsg.length()));
            }

            if (err == ESP_OK)
            {
                err = radio.clearIrq(0xFFFF);
            }

            if (err == ESP_OK)
            {
                ESP_LOGI(TAG, "TX => %s", txMsg.c_str());
                err = radio.setTx(0x000000);
            }

            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "TX prepare failed: %s", esp_err_to_name(err));
                xSemaphoreGive(state.radioMutex());
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }

            uint32_t wait_ms = 0;

            while (!radio.isDio1High())
            {
                vTaskDelay(pdMS_TO_TICKS(10));
                wait_ms += 10;

                if (wait_ms >= 5000)
                {
                    ESP_LOGW(TAG, "wait DIO1 timeout");
                    break;
                }
            }

            uint16_t irq = 0;
            err = radio.getIrqStatus(&irq);

            if (err == ESP_OK)
            {
                ESP_LOGI(TAG, "IRQ = 0x%04X", irq);
                radio.clearIrq(irq);

                if (irq & LLCC68::IRQ_TX_DONE)
                {
                    ESP_LOGI(TAG, "TX done");
                }

                if (irq & LLCC68::IRQ_TIMEOUT)
                {
                    ESP_LOGW(TAG, "TX timeout");
                }
            }
            else
            {
                ESP_LOGE(TAG, "getIrqStatus failed: %s", esp_err_to_name(err));
            }

            xSemaphoreGive(state.radioMutex());
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        /*
         * 接收模式
         * 这里假设你的 LLCC68 具有接收相关接口：
         * - setRx(...)
         * - readBuffer(...)
         * - getIrqStatus(...)
         * - clearIrq(...)
         * 如果你的驱动接口名字不同，替换成你实际的接口即可。
         */
        if (state.isLoraRxMode())
        {
            xSemaphoreTake(state.radioMutex(), portMAX_DELAY);

            esp_err_t err = radio.setPacketParams(255);

            if (err == ESP_OK)
            {
                err = radio.clearIrq(0xFFFF);
            }

            if (err == ESP_OK)
            {
                err = radio.setRx(0x000000);
            }

            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "setRx failed: %s", esp_err_to_name(err));
                xSemaphoreGive(state.radioMutex());
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }

            /*
             * 等待中断
             */
            uint32_t wait_ms = 0;
            while (!radio.isDio1High())
            {
                vTaskDelay(pdMS_TO_TICKS(10));
                wait_ms += 10;

                if (wait_ms >= 1000)
                {
                    /*
                     * 接收超时后继续下一轮
                     * 如果你希望连续接收，也可以不做超时退出。
                     */
                    break;
                }
            }

            uint16_t irq = 0;
            err = radio.getIrqStatus(&irq);

            if (err == ESP_OK)
            {
                if (irq != 0)
                {
                    ESP_LOGI(TAG, "RX IRQ = 0x%04X", irq);
                }

                if (irq & LLCC68::IRQ_RX_DONE)
                {
                    uint8_t buf[255] = {};
                    uint8_t len = 0;

                    /*
                     * 这里按你驱动实际接口调整：
                     * 常见是 readBuffer(offset, buf, len)
                     * 或者 readBuffer(buf, len)
                     */
                    err = radio.readBuffer(buf, &len);

                    if (err == ESP_OK && len > 0)
                    {
                        std::string msg(
                            reinterpret_cast<char *>(buf),
                            reinterpret_cast<char *>(buf) + len);

                        /*
                         * 这里 RSSI/SNR 如果你的驱动有接口可以取，
                         * 没有的话可以先传 0。
                         */
                        int rssi = 0;
                        float snr = 0.0f;

                        radio.getPacketStatus(&rssi, &snr);

                        state.pushRxMessage(msg, rssi, snr);

                        ESP_LOGI(TAG, "RX => %s", msg.c_str());
                    }
                    else
                    {
                        ESP_LOGW(TAG, "readBuffer failed or empty");
                    }
                }

                if (irq & LLCC68::IRQ_TIMEOUT)
                {
                    ESP_LOGW(TAG, "RX timeout");
                }

                radio.clearIrq(irq);
            }
            else
            {
                ESP_LOGE(TAG, "getIrqStatus failed: %s", esp_err_to_name(err));
            }

            xSemaphoreGive(state.radioMutex());

            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ---------------- app_main ---------------- */
extern "C" void app_main(void)
{
    esp_err_t err;

    AppState &state = AppState::instance();

    if (!state.init())
    {
        ESP_LOGE(TAG, "AppState init failed");
        return;
    }

    /*
     * NVS
     */
    err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    else
    {
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
     */
    static WebServer webServer;
    ESP_ERROR_CHECK(webServer.start());

    /*
     * LoRa 任务：
     * - 单核芯片，例如 ESP32-C5：不指定核心
     * - 多核芯片，例如 ESP32 / ESP32-S3：固定到 CPU1
     */
#if CONFIG_FREERTOS_UNICORE
    BaseType_t ret = xTaskCreate(
        lora_task,
        "lora_task",
        6144,
        nullptr,
        5,
        nullptr);
#else
    BaseType_t ret = xTaskCreatePinnedToCore(
        lora_task,
        "lora_task",
        6144,
        nullptr,
        5,
        nullptr,
        1);
#endif

    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "create lora_task failed");
        return;
    }

    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "create lora_task failed");
        return;
    }

    ESP_LOGI(TAG, "app_main done");
}