#include "camera/UnitCamS3_5MP.h"

void UnitCamS3_5MP::SetLed(bool state) {
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
    _config.timeZone = doc["timeZone"].as<std::string>();

    _config.postInterval = doc["postInterval"];
    _config.postServer = doc["postServer"].as<std::string>();
    _config.postPort = doc["postPort"];

    _config.jpegQuantity = doc["jpegQuantity"];
    _config.frameSize = doc["frameSize"].as<framesize_t>();

    file.close();
}

void UnitCamS3_5MP::SaveConfig() {
    ESP_LOGI(TAG, "save system config");

    // Try open
    File file = LittleFS.open(CONFIG_FILE_PATH, "w");
    if (!file) {
        ESP_LOGI(TAG, "open {%s} failed", CONFIG_FILE_PATH);
        return;
    }

    // Parse json
    JsonDocument doc;

    doc["wifiSsid"] = _config.wifiSsid;
    doc["wifiPass"] = _config.wifiPass;
    doc["startPoster"] = _config.startPoster;
    doc["waitApFirst"] = _config.waitApFirst;
    doc["nickname"] = _config.nickname;
    doc["timeZone"] = _config.timeZone;

    doc["postInterval"] = _config.postInterval;
    doc["postServer"] = _config.postServer;
    doc["postPort"] = _config.postPort;

    doc["jpegQuantity"] = _config.jpegQuantity;
    doc["frameSize"] = _config.frameSize;

    serializeJson(doc, file);

    file.close();
}

CONFIG::SystemConfig_t UnitCamS3_5MP::GetConfig() {
    return _config;
}

void UnitCamS3_5MP::SetConfig(CONFIG::SystemConfig_t config, CONFIG::ConfigType type) {
    _config = config;
    if ((type & CONFIG::ConfigType::APP) == CONFIG::ConfigType::APP) {
    } else if ((type & CONFIG::ConfigType::CAMERA) == CONFIG::ConfigType::CAMERA) {
        cam_init();
    } else if ((type & CONFIG::ConfigType::WIFI) == CONFIG::ConfigType::WIFI) {
    }
}

void UnitCamS3_5MP::TakePhoto(std::function<void(camera_fb_t *buffer)> processPhoto) {
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
    if (processPhoto) {
        processPhoto(fb);
    }
    esp_camera_fb_return(fb);
    fb = NULL;
    if (OnTakePhotoEnd != nullptr) {
        OnTakePhotoEnd();
    }
    SetLed(0);
}