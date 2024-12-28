#include "camera/UnitCamS3_5MP.h"

void UnitCamS3_5MP::SetLed(bool state){
    gpio_set_level((gpio_num_t)HAL_PIN_LED, state ? 0 : 1);
}


void UnitCamS3_5MP::LoadConfig() {
    ESP_LOGI(TAG, "load config");

    // Check exist
    if (!LittleFS.exists(CONFIG_FILE_PATH)) {
        ESP_LOGI(TAG, "file doesn't exist! %s", CONFIG_FILE_PATH);
        return;
    }

    // Try open
    File file = LittleFS.open(CONFIG_FILE_PATH, "r");
    if (!file) {
        return;
    }

    // Parse json
    JsonDocument doc;
    deserializeJson(doc, file);

    // Copy into buffer
    _config.wifiSsid = doc["wifiSsid"].as<std::string>();
    _config.wifiPass = doc["wifiPass"].as<std::string>();
    _config.startPoster = doc["startPoster"].as<std::string>();
    _config.waitApFirst = doc["waitApFirst"].as<std::string>();
    _config.nickname = doc["nickname"].as<std::string>();
    _config.postInterval = doc["postInterval"];
    _config.timeZone = doc["timeZone"].as<std::string>();

    file.close();
}

void UnitCamS3_5MP::SaveConfig(){
      ESP_LOGI(TAG,"save system config");

    // Try open
    File file = LittleFS.open(CONFIG_FILE_PATH, "w");
    if (!file)
    {
        ESP_LOGI(TAG,"open {} failed", CONFIG_FILE_PATH);
        return;
    }

    // Parse json
    JsonDocument doc;

    doc["wifiSsid"] = _config.wifiSsid;
    doc["wifiPass"] = _config.wifiPass;
    doc["startPoster"] = _config.startPoster;
    doc["waitApFirst"] = _config.waitApFirst;
    doc["nickname"] = _config.nickname;
    doc["postInterval"] = _config.postInterval;
    doc["timeZone"] = _config.timeZone;

    serializeJson(doc, file);

    file.close();
}

void UnitCamS3_5MP::TakePhoto() {
    camera_fb_t *fb = NULL;
    if (OnTakePhotoStart != nullptr) {
        OnTakePhotoStart();
    }
    SetLed(1);
    fb = esp_camera_fb_get();
    if (!fb) {
        SetLed(0);
        return;
    }
    ESP_LOGI(TAG, "captrued height: %d , width: %d, length: %d", fb->height, fb->width, fb->len);
    
    esp_camera_fb_return(fb);
    fb = NULL;
    if (OnTakePhotoEnd != nullptr) {
        OnTakePhotoEnd();
    }
    SetLed(0);
}