#pragma once

#include <string>
#include <functional>
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "WebServerService.h"

// 定义 WiFi 事件位（避免与 common.h 冲突）
#define WIFI_SERVICE_CONNECTED_BIT BIT0
#define WIFI_SERVICE_FAIL_BIT BIT1
#define WIFI_SERVICE_AP_MODE BIT2

class WifiService {
public:
    static WifiService& getInstance() {
        static WifiService instance;
        return instance;
    }

    // 禁止拷贝
    WifiService(const WifiService&) = delete;
    WifiService& operator=(const WifiService&) = delete;

    void init(const std::string& ssid, const std::string& password);
    void connect(bool forceAP = false);
    void startAPMode();
    void disconnect();
    bool isConnected();
    bool isAPMode() const { return _apMode; }
    
    // 事件回调
    std::function<void()> onConnected;
    std::function<void()> onDisconnected;
    std::function<void(int retryCount)> onConnecting;
    std::function<void()> onAPModeStarted;

private:
    WifiService() = default;
    ~WifiService() = default;

    // WiFi 事件处理
    static void wifiEventCallback(void* arg, esp_event_base_t event_base,
                                  int32_t event_id, void* event_data);
    
    // 内部方法
    void handleWifiConnected();
    void handleWifiDisconnected(int reason);
    void handleGotIP();
    void handleAPModeStarted();
    
    // 成员变量
    std::string _ssid;
    std::string _password;
    
    EventGroupHandle_t _eventGroup;
    int _retryCount;
    bool _apMode = false;
    bool _initialized = false;
    
    static const int MAX_RETRY = 5;
};