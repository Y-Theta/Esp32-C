#include "camera/UnitCamS3_5MP.h"
#include "services/WifiService.h"
#include "services/StorageService.h"
#include "esp_log.h"

static const char* TAG = "UnitCamS3_5MP";

void UnitCamS3_5MP::Start() {
    // 启动时直接进入 AP 模式（用于 Web 配置/相机功能）
    StartForSetting();
}

void UnitCamS3_5MP::StartForSetting() {
    StorageService& storage = StorageService::getInstance();

    // 初始化 WiFi - AP 模式
    WifiService& wifi = WifiService::getInstance();
    wifi.init(storage.getConfig().wifiSsid, storage.getConfig().wifiPass);

    // 设置 WiFi 事件回调
    wifi.onConnected = []() {
        ESP_LOGI(TAG, "WiFi connected (STA mode)");
        // STA 模式下，根据需要决定是否启动相机
    };

    wifi.onAPModeStarted = []() {
        ESP_LOGI(TAG, "AP mode started");
        // AP 模式下，预初始化相机（为 Web UVC 准备）
    };

    // 启动 WiFi（优先 AP 模式）
    wifi.connect(true); // true = 强制 AP 模式

    while (true) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

void UnitCamS3_5MP::StartForWorking() {
    // STA 模式工作逻辑（保留但暂不自动使用）
    StorageService& storage = StorageService::getInstance();

    WifiService& wifi = WifiService::getInstance();
    wifi.init(storage.getConfig().wifiSsid, storage.getConfig().wifiPass);

    wifi.onConnected = []() {
        ESP_LOGI(TAG, "WiFi connected (STA mode), camera ready");
    };

    wifi.connect();

    while (true) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

// 供 Web 服务调用的拍照函数
void UnitCamS3_5MP::WebTakePhoto(std::function<void(camera_fb_t* buffer)> processPhoto) {
    if (!_initialized) {
        ESP_LOGI(TAG, "Camera not initialized, initializing...");
        cam_init();
    }

    ESP_LOGI(TAG, "Taking photo via web...");
    TakePhoto(processPhoto ? processPhoto : [](camera_fb_t *fb) {
        // 照片数据会在 Web 服务中处理
        ESP_LOGI(TAG, "Photo taken: %ux%u, %u bytes", fb->width, fb->height, fb->len);
    });
}