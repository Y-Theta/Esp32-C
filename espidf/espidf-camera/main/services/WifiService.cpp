#include "services/WifiService.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "freertos/task.h"

static const char* TAG = "WifiService";

void WifiService::init(const std::string& ssid, const std::string& password) {
    _ssid = ssid;
    _password = password;
    _eventGroup = xEventGroupCreate();
    _retryCount = 0;
}

void WifiService::connect() {
    ESP_LOGI(TAG, "Connecting to SSID: %s", _ssid.c_str());
    
    // 初始化 NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // 初始化网络
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    
    // 注册事件处理器
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifiEventCallback,
                                                        this,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifiEventCallback,
                                                        this,
                                                        NULL));
    
    // 初始化 WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // 配置 WiFi
    wifi_config_t wifi_config = {};
    strlcpy((char*)wifi_config.sta.ssid, _ssid.c_str(), sizeof(wifi_config.sta.ssid));
    strlcpy((char*)wifi_config.sta.password, _password.c_str(), sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.scan_method = WIFI_FAST_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    
    // 等待连接
    EventBits_t bits = xEventGroupWaitBits(_eventGroup,
                                            WIFI_SERVICE_CONNECTED_BIT | WIFI_SERVICE_FAIL_BIT,
                                            pdFALSE,
                                            pdFALSE,
                                            pdMS_TO_TICKS(300000)); // 5 分钟超时
    
    if (bits & WIFI_SERVICE_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected successfully!");
    } else {
        ESP_LOGE(TAG, "WiFi connection timeout!");
    }
}

void WifiService::disconnect() {
    esp_wifi_stop();
}

bool WifiService::isConnected() {
    EventBits_t bits = xEventGroupGetBits(_eventGroup);
    return (bits & WIFI_SERVICE_CONNECTED_BIT) != 0;
}

void WifiService::wifiEventCallback(void* arg, esp_event_base_t event_base,
                                   int32_t event_id, void* event_data) {
    WifiService* service = static_cast<WifiService*>(arg);
    
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
            ESP_LOGI(TAG, "WiFi connected to AP!");
            service->handleWifiConnected();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_event_sta_disconnected_t* event = 
                static_cast<wifi_event_sta_disconnected_t*>(event_data);
            service->handleWifiDisconnected(event->reason);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = static_cast<ip_event_got_ip_t*>(event_data);
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        service->handleGotIP();
    }
}

void WifiService::handleWifiConnected() {
    _retryCount = 0;
    if (onConnected) onConnected();
}

void WifiService::handleWifiDisconnected(int reason) {
    ESP_LOGI(TAG, "WiFi disconnected, reason: %d", reason);
    
    if (_retryCount < MAX_RETRY) {
        _retryCount++;
        ESP_LOGI(TAG, "Retrying WiFi connection... (%d/%d)", _retryCount, MAX_RETRY);
        
        if (onConnecting) onConnecting(_retryCount);
        
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_wifi_connect();
    } else {
        ESP_LOGI(TAG, "Max retries reached!");
        xEventGroupSetBits(_eventGroup, WIFI_SERVICE_FAIL_BIT);
        if (onDisconnected) onDisconnected();
    }
}

void WifiService::handleGotIP() {
    xEventGroupSetBits(_eventGroup, WIFI_SERVICE_CONNECTED_BIT);
}