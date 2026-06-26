#include "services/WifiService.h"
#include "services/WebServerService.h"
#include "services/StorageService.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include <string>
#include <arpa/inet.h>

static const char* TAG = "WifiService";

void WifiService::init() {
    _currentWifiIndex = 0;
    _retryCount = 0;
    _apMode = false;
    
    if (!_eventGroup) {
        _eventGroup = xEventGroupCreate();
    }
    
    if (!_initialized) {
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        ESP_ERROR_CHECK(ret);
        
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_create_default_wifi_sta();
        esp_netif_create_default_wifi_ap();
        
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
        
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        
        _initialized = true;
    }
}

bool WifiService::connectNextWifi() {
    StorageService& storage = StorageService::getInstance();
    const CONFIG::SystemConfig_t& config = storage.getConfig();
    
    // 找到下一个非空的WiFi配置
    while (_currentWifiIndex < MAX_WIFI_SSIDS) {
        if (!config.wifiSsid[_currentWifiIndex].empty()) {
            ESP_LOGI(TAG, "Trying WiFi %d: SSID=%s", _currentWifiIndex + 1, config.wifiSsid[_currentWifiIndex].c_str());
            
            wifi_config_t wifi_config = {};
            strlcpy((char*)wifi_config.sta.ssid, config.wifiSsid[_currentWifiIndex].c_str(), sizeof(wifi_config.sta.ssid));
            strlcpy((char*)wifi_config.sta.password, config.wifiPass[_currentWifiIndex].c_str(), sizeof(wifi_config.sta.password));
            wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
            wifi_config.sta.scan_method = WIFI_FAST_SCAN;
            wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
            wifi_config.sta.pmf_cfg.capable = true;
            wifi_config.sta.pmf_cfg.required = false;
            
            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
            esp_wifi_connect();
            
            _retryCount = 0;
            return true;
        }
        _currentWifiIndex++;
    }
    
    return false;
}

void WifiService::connect(bool forceAP) {
    if (_apMode) {
        ESP_LOGI(TAG, "Already in AP mode");
        if (!forceAP) {
            return;
        }
    }
    
    if (forceAP) {
        ESP_LOGI(TAG, "Forcing AP mode");
        startAPMode();
        return;
    }
    
    _currentWifiIndex = 0;
    _apMode = false;
    
    if (!connectNextWifi()) {
        ESP_LOGI(TAG, "No WiFi configured, starting AP mode");
        startAPMode();
        return;
    }
    
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    
    // 等待连接，循环尝试所有配置的SSID
    while (true) {
        EventBits_t bits = xEventGroupWaitBits(_eventGroup,
                                                WIFI_SERVICE_CONNECTED_BIT | WIFI_SERVICE_FAIL_BIT,
                                                pdFALSE,
                                                pdFALSE,
                                                pdMS_TO_TICKS(15000));
        
        if (bits & WIFI_SERVICE_CONNECTED_BIT) {
            ESP_LOGI(TAG, "WiFi connected successfully!");
            return;
        }
        
        // 连接失败，尝试下一个SSID
        _currentWifiIndex++;
        xEventGroupClearBits(_eventGroup, WIFI_SERVICE_FAIL_BIT);
        
        if (!connectNextWifi()) {
            ESP_LOGI(TAG, "All WiFi configurations failed, starting AP mode");
            startAPMode();
            return;
        }
    }
}

void WifiService::startAPMode() {
    ESP_LOGI(TAG, "Starting WiFi AP mode...");
    _apMode = true;
    _retryCount = 0;
    
    esp_wifi_stop();
    
    esp_netif_ip_info_t ip_info;
    memset(&ip_info, 0, sizeof(ip_info));
    
    ip_info.ip.addr = inet_addr("172.20.0.1");
    ip_info.gw.addr = ip_info.ip.addr;
    ip_info.netmask.addr = inet_addr("255.255.255.0");
    
    esp_netif_t* ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(ap_netif));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(ap_netif, &ip_info));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(ap_netif));
    
    wifi_config_t ap_config = {};
    strlcpy((char*)ap_config.ap.ssid, "M5Stack 5MP Cam", sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = strlen("M5Stack 5MP Cam");
    ap_config.ap.channel = 1;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    strlcpy((char*)ap_config.ap.password, "20154530", sizeof(ap_config.ap.password));
    ap_config.ap.max_connection = 4;
    ap_config.ap.beacon_interval = 100;
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    xEventGroupSetBits(_eventGroup, WIFI_SERVICE_AP_MODE);
    handleAPModeStarted();
}

void WifiService::handleAPModeStarted() {
    ESP_LOGI(TAG, "AP mode started! SSID: M5Stack 5MP Cam");
    ESP_LOGI(TAG, "Connect to WiFi 'M5Stack 5MP Cam' (password: 20154530) and visit http://172.20.0.1");
    
    WebServerService& webServer = WebServerService::getInstance();
    webServer.start();
    
    if (onAPModeStarted) {
        onAPModeStarted();
    }
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

static void wifiReconnectTask(void* arg) {
    WifiService* service = static_cast<WifiService*>(arg);
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_wifi_connect();
    vTaskDelete(nullptr);
}

void WifiService::handleWifiDisconnected(int reason) {
    ESP_LOGI(TAG, "WiFi disconnected, reason: %d", reason);
    
    if (_retryCount < MAX_RETRY_PER_SSID) {
        _retryCount++;
        ESP_LOGI(TAG, "Retrying WiFi connection... (%d/%d for SSID %d)", 
                 _retryCount, MAX_RETRY_PER_SSID, _currentWifiIndex + 1);
        
        if (onConnecting) onConnecting(_retryCount);
        
        xTaskCreate(wifiReconnectTask, "wifi_recon", 2048, this, tskIDLE_PRIORITY, nullptr);
    } else {
        ESP_LOGI(TAG, "Max retries reached for this SSID, trying next...");
        xEventGroupSetBits(_eventGroup, WIFI_SERVICE_FAIL_BIT);
    }
}

void WifiService::handleGotIP() {
    xEventGroupSetBits(_eventGroup, WIFI_SERVICE_CONNECTED_BIT);
    handleWifiConnected();
}