#include "camera/UnitCamS3_5MP.h"

void UnitCamS3_5MP::Start() {
    if (!_config.wifiSsid.empty()) {
        StartForWorking();
    } else {
        StartForSetting();
    }

    _config.wifiSsid = "";
    SaveConfig();
}

static void task_take_photo(void *parameters) {
    UnitCamS3_5MP *ump = (UnitCamS3_5MP *)parameters;

    while (true) {
        ESP_LOGI(TAG, "Taking photo...");
        ump->TakePhoto([ump](camera_fb_t *fb) -> void {
            if (ump->OnProcessImage) {
                ump->OnProcessImage(fb, ump);
            }
        });
        vTaskDelay(ump->GetConfig().postInterval * 1000 / portTICK_PERIOD_MS);
    }
}

void UnitCamS3_5MP::StartForWorking() {
    connect_wifi();
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