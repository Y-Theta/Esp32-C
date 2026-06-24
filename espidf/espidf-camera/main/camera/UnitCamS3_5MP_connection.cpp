#include "camera/UnitCamS3_5MP.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>

static EventGroupHandle_t s_wifi_event_group;
static const int WIFI_GOT_IP_BIT_LOCAL = BIT1;
static const int WIFI_STA_STARTED_BIT = BIT0;

static int s_retry_num = 0;
#define MAX_RETRY 10
static bool s_scan_done = false;

static void wifi_worker_task(void *parameter) {
    ESP_LOGI(TAG, "WiFi worker task started");
    
    xEventGroupWaitBits(s_wifi_event_group, WIFI_STA_STARTED_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
    
    ESP_LOGI(TAG, "WiFi STA started");
    
    uint16_t number = 10;
    wifi_ap_record_t ap_info[10];
    uint16_t ap_count = 0;
    memset(ap_info, 0, sizeof(ap_info));

    ESP_LOGI(TAG, "Scanning for WiFi networks...");
    esp_wifi_scan_start(NULL, true);
    esp_wifi_scan_get_ap_records(&number, ap_info);
    esp_wifi_scan_get_ap_num(&ap_count);
    ESP_LOGI(TAG, "Found %d APs", ap_count);
    
    for (int i = 0; i < ap_count; i++) {
        ESP_LOGI(TAG, "SSID: %s, Channel: %d, RSSI: %d", ap_info[i].ssid, ap_info[i].primary, ap_info[i].rssi);
    }
    s_scan_done = true;
    
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "Starting connection...");
    esp_wifi_connect();
    
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_STA_STARTED_BIT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "WiFi connected to AP!");
        s_retry_num = 0;
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* disconnected = (wifi_event_sta_disconnected_t*)event_data;
        ESP_LOGI(TAG, "WiFi disconnected, reason: %d", disconnected->reason);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_GOT_IP_BIT_LOCAL);
    }
}

void UnitCamS3_5MP::connect_wifi() {
    ESP_LOGI(TAG, "start wifi STA mode!");
    ESP_LOGI(TAG, "Connecting to SSID: %s", _config.wifiSsid.c_str());
    
    s_retry_num = 0;
    s_scan_done = false;
    
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {};
    strlcpy((char *)wifi_config.sta.ssid, _config.wifiSsid.c_str(), sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, _config.wifiPass.c_str(), sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.scan_method = WIFI_FAST_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    xTaskCreate(&wifi_worker_task, "wifi_worker", 4096, NULL, 5, NULL);
    
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_GOT_IP_BIT_LOCAL, pdFALSE, pdTRUE, pdMS_TO_TICKS(120000));
    if (bits & WIFI_GOT_IP_BIT_LOCAL) {
        ESP_LOGI(TAG, "WiFi connected and got IP successfully!");
    } else {
        ESP_LOGI(TAG, "WiFi connect timeout");
        ESP_LOGI(TAG, "Check SSID/password or WiFi router settings");
        ESP_LOGI(TAG, "Make sure WiFi is 2.4GHz (not 5GHz)");
        ESP_LOGI(TAG, "Restarting...");
        esp_restart();
    }
}

void UnitCamS3_5MP::disconnect_wifi() {
    esp_wifi_stop();
}

void UnitCamS3_5MP::setup_ap() {
}

void UnitCamS3_5MP::close_ap() {
}