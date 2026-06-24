#include "camera/UnitCamS3_5MP.h"
#include "services/WifiService.h"
#include "services/SettingService.h"
#include "esp_log.h"

static const char* TAG = "UnitCamS3_5MP";

void UnitCamS3_5MP::Start() {
    SettingService& settings = SettingService::getInstance();
    if (!settings.getConfig().wifiSsid.empty()) {
        StartForWorking();
    } else {
        StartForSetting();
    }
}

static void task_take_photo(void *parameters) {
    UnitCamS3_5MP *ump = (UnitCamS3_5MP *)parameters;
    SettingService& settings = SettingService::getInstance();

    while (true) {
        ESP_LOGI(TAG, "Taking photo...");
        ump->TakePhoto([ump](camera_fb_t *fb) -> void {
            if (ump->OnProcessImage) {
                ump->OnProcessImage(fb, ump);
            }
        });
        vTaskDelay(settings.getConfig().postInterval * 1000 / portTICK_PERIOD_MS);
    }
}

void UnitCamS3_5MP::StartForWorking() {
    SettingService& settings = SettingService::getInstance();
    
    // 初始化并连接 WiFi
    WifiService& wifi = WifiService::getInstance();
    wifi.init(settings.getConfig().wifiSsid, settings.getConfig().wifiPass);
    wifi.connect();
    
    cam_init();
    
    BaseType_t ret = xTaskCreate(task_take_photo, "photo", 5 * 1024, this, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGI(TAG, "Failed to create photo task");
    } else {
        ESP_LOGI(TAG, "Photo task created successfully");
    }
    while (true) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}