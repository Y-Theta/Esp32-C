#include "services/WifiService.h"
#include "services/WebServerService.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "freertos/task.h"
#include <string>
#include <arpa/inet.h>

static const char* TAG = "WifiService";

void WifiService::init(const std::string& ssid, const std::string& password) {
    _ssid = ssid;
    _password = password;
    _eventGroup = xEventGroupCreate();
    _retryCount = 0;
    _apMode = false;
    
    if (!_initialized) {
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
        esp_netif_create_default_wifi_ap();
        
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
        
        _initialized = true;
    }
}

void WifiService::connect() {
    if (_apMode) {
        ESP_LOGI(TAG, "Already in AP mode, stopping AP first");
        esp_wifi_stop();
        _apMode = false;
    }
    
    ESP_LOGI(TAG, "Connecting to SSID: %s", _ssid.c_str());
    
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
                                            pdMS_TO_TICKS(30000)); // 30秒超时
    
    if (bits & WIFI_SERVICE_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected successfully!");
    } else {
        ESP_LOGI(TAG, "WiFi connection failed, starting AP mode");
        startAPMode();
    }
}

void WifiService::startAPMode() {
    ESP_LOGI(TAG, "Starting WiFi AP mode...");
    _apMode = true;
    _retryCount = 0;
    
    // 停止之前的连接
    esp_wifi_stop();
    
    // 配置 AP 网络
    esp_netif_ip_info_t ip_info;
    memset(&ip_info, 0, sizeof(ip_info));
    
    // 设置 IP 地址为 172.20.0.1 (网络字节序)
    ip_info.ip.addr = inet_addr("172.20.0.1");
    ip_info.gw.addr = ip_info.ip.addr;
    ip_info.netmask.addr = inet_addr("255.255.255.0");
    
    esp_netif_t* ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(ap_netif));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(ap_netif, &ip_info));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(ap_netif));
    
    // 配置 AP 模式
    wifi_config_t ap_config = {};
    strlcpy((char*)ap_config.ap.ssid, "UnitCamS3_Config", sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = strlen("UnitCamS3_Config");
    ap_config.ap.channel = 1;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
    ap_config.ap.max_connection = 4;
    ap_config.ap.beacon_interval = 100;
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    xEventGroupSetBits(_eventGroup, WIFI_SERVICE_AP_MODE);
    handleAPModeStarted();
}

void WifiService::handleAPModeStarted() {
    ESP_LOGI(TAG, "AP mode started! SSID: UnitCamS3_Config");
    ESP_LOGI(TAG, "Connect to WiFi 'UnitCamS3_Config' and visit http://172.20.0.1");
    
    // 启动 Web 服务器
    WebServerService& webServer = WebServerService::getInstance();
    webServer.start();
    
    if (onAPModeStarted) {
        onAPModeStarted();
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
            if (!service->_apMode) {
                esp_wifi_connect();
            }
        } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
            ESP_LOGI(TAG, "WiFi connected to AP!");
            service->handleWifiConnected();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            if (!service->_apMode) {
                wifi_event_sta_disconnected_t* event = 
                    static_cast<wifi_event_sta_disconnected_t*>(event_data);
                service->handleWifiDisconnected(event->reason);
            }
        } else if (event_id == WIFI_EVENT_AP_START) {
            ESP_LOGI(TAG, "WiFi AP started");
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP && !service->_apMode) {
            ip_event_got_ip_t* event = static_cast<ip_event_got_ip_t*>(event_data);
            ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
            ESP_LOGI(TAG, "You can now visit the configuration page at http://" IPSTR, IP2STR(&event->ip_info.ip));
            
            // 启动 Web 服务器
            WebServerService& webServer = WebServerService::getInstance();
            webServer.start();
            
            service->handleGotIP();
        }
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
        ESP_LOGI(TAG, "Max retries reached, starting AP mode");
        xEventGroupSetBits(_eventGroup, WIFI_SERVICE_FAIL_BIT);
        startAPMode();
    }
}

void WifiService::handleGotIP() {
    xEventGroupSetBits(_eventGroup, WIFI_SERVICE_CONNECTED_BIT);
}