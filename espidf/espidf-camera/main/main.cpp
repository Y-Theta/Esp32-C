#include "camera/UnitCamS3_5MP.h"
#include "services/StorageService.h"
#include "esp_log.h"

static const char* TAG = "ESP32Camera";

extern "C" void app_main(void) {
    vTaskDelay(3000 / portTICK_PERIOD_MS);

    StorageService& storage = StorageService::getInstance();
    storage.init();
    ESP_LOGI(TAG, "Storage initialized");

    UnitCamS3_5MP& camera = UnitCamS3_5MP::getInstance();
    camera.Init();

    camera.Start();
}