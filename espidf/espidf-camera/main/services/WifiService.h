#pragma once

#include <string>
#include <functional>
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "WebServerService.h"
#include "common.h"

#define WIFI_SERVICE_CONNECTED_BIT BIT0
#define WIFI_SERVICE_FAIL_BIT BIT1
#define WIFI_SERVICE_AP_MODE BIT2

class WifiService {
public:
    static WifiService& getInstance() {
        static WifiService instance;
        return instance;
    }

    WifiService(const WifiService&) = delete;
    WifiService& operator=(const WifiService&) = delete;

    void init();
    void connect(bool forceAP = false);
    void startAPMode();
    bool isConnected();
    bool isAPMode() const { return _apMode; }
    
    std::function<void()> onConnected;
    std::function<void(int retryCount)> onConnecting;
    std::function<void()> onAPModeStarted;

private:
    WifiService() = default;
    ~WifiService() = default;

    static void wifiEventCallback(void* arg, esp_event_base_t event_base,
                                  int32_t event_id, void* event_data);
    
    void handleWifiConnected();
    void handleWifiDisconnected(int reason);
    void handleGotIP();
    void handleAPModeStarted();
    bool connectNextWifi();
    
    int _currentWifiIndex = 0;
    EventGroupHandle_t _eventGroup = nullptr;
    int _retryCount = 0;
    bool _apMode = false;
    bool _initialized = false;
    
    static const int MAX_RETRY_PER_SSID = 3;
};