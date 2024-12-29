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
        ump->TakePhoto([&ump](camera_fb_t *fb) -> void {
            ump->OnProcessImage(fb, ump);
            return;
        });
        vTaskDelay(ump->GetConfig().postInterval * 1000);
    }
}

void UnitCamS3_5MP::StartForWorking() {
    connect_wifi();
    UnitCamS3_5MP* me = this; 
    xTaskCreate(task_take_photo, "photo", 5 * 1024, this, 5, NULL);
    while (true) {
        vTaskDelay(500);
    }
}
