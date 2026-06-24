#include "camera/UnitCamS3_5MP.h"
#include "services/WifiService.h"
#include "services/StorageService.h"
#include "esp_log.h"

static const char* TAG = "UnitCamS3_5MP";
static UnitCamS3_5MP* g_camera = nullptr;
static TaskHandle_t g_photoTask = nullptr;

void UnitCamS3_5MP::Start() {
    StorageService& storage = StorageService::getInstance();
    if (!storage.getConfig().wifiSsid.empty()) {
        StartForWorking();
    } else {
        StartForSetting();
    }
}

static void task_take_photo(void *parameters) {
    UnitCamS3_5MP *ump = (UnitCamS3_5MP *)parameters;
    StorageService& storage = StorageService::getInstance();

    while (true) {
        WifiService& wifi = WifiService::getInstance();
        
        if (wifi.isConnected()) {
            ESP_LOGI(TAG, "Taking photo...");
            ump->TakePhoto([ump](camera_fb_t *fb) -> void {
                if (ump->OnProcessImage) {
                    ump->OnProcessImage(fb, ump);
                }
            });
            vTaskDelay(storage.getConfig().postInterval * 1000 / portTICK_PERIOD_MS);
        } else {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }
}

void UnitCamS3_5MP::StartForWorking() {
    StorageService& storage = StorageService::getInstance();
    g_camera = this;
    
    // 初始化并连接 WiFi
    WifiService& wifi = WifiService::getInstance();
    wifi.init(storage.getConfig().wifiSsid, storage.getConfig().wifiPass);
    
    // 设置 WiFi 事件回调
    wifi.onConnected = []() {
        ESP_LOGI(TAG, "WiFi connected, starting camera...");
        if (g_camera) {
            g_camera->cam_init();
            
            if (!g_photoTask) {
                BaseType_t ret = xTaskCreate(task_take_photo, "photo", 5 * 1024, g_camera, 5, &g_photoTask);
                if (ret != pdPASS) {
                    ESP_LOGI(TAG, "Failed to create photo task");
                } else {
                    ESP_LOGI(TAG, "Photo task created successfully");
                }
            }
        }
    };
    
    wifi.onAPModeStarted = []() {
        ESP_LOGI(TAG, "AP mode started, camera task stopped");
        if (g_photoTask) {
            vTaskDelete(g_photoTask);
            g_photoTask = nullptr;
        }
    };
    
    wifi.connect();
    
    while (true) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}