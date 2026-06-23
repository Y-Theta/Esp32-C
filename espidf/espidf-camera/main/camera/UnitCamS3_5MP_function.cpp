#include "camera/UnitCamS3_5MP.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void UnitCamS3_5MP::SetLed(bool state) {
    gpio_set_level((gpio_num_t)HAL_PIN_LED, state ? 0 : 1);
}

void UnitCamS3_5MP::LoadConfig() {
    ESP_LOGI(TAG, "load config");

    FILE *file = fopen(CONFIG_FILE_PATH, "r");
    if (!file) {
        ESP_LOGI(TAG, "file doesn't exist! %s", CONFIG_FILE_PATH);
        return;
    }

    fseek(file, 0, SEEK_END);
    long fsize = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *buffer = (char *)malloc(fsize + 1);
    fread(buffer, 1, fsize, file);
    buffer[fsize] = '\0';
    fclose(file);

    cJSON *doc = cJSON_Parse(buffer);
    free(buffer);

    if (!doc) {
        ESP_LOGI(TAG, "json parse failed");
        return;
    }

    cJSON *item = cJSON_GetObjectItem(doc, "wifiSsid");
    if (item && cJSON_IsString(item)) {
        _config.wifiSsid = item->valuestring;
    }

    item = cJSON_GetObjectItem(doc, "wifiPass");
    if (item && cJSON_IsString(item)) {
        _config.wifiPass = item->valuestring;
    }

    item = cJSON_GetObjectItem(doc, "startPoster");
    if (item && cJSON_IsString(item)) {
        _config.startPoster = item->valuestring;
    }

    item = cJSON_GetObjectItem(doc, "waitApFirst");
    if (item && cJSON_IsString(item)) {
        _config.waitApFirst = item->valuestring;
    }

    item = cJSON_GetObjectItem(doc, "nickname");
    if (item && cJSON_IsString(item)) {
        _config.nickname = item->valuestring;
    }

    item = cJSON_GetObjectItem(doc, "timeZone");
    if (item && cJSON_IsString(item)) {
        _config.timeZone = item->valuestring;
    }

    item = cJSON_GetObjectItem(doc, "postInterval");
    if (item && cJSON_IsNumber(item)) {
        _config.postInterval = item->valueint;
    }

    item = cJSON_GetObjectItem(doc, "postServer");
    if (item && cJSON_IsString(item)) {
        _config.postServer = item->valuestring;
    }

    item = cJSON_GetObjectItem(doc, "postPort");
    if (item && cJSON_IsNumber(item)) {
        _config.postPort = item->valueint;
    }

    item = cJSON_GetObjectItem(doc, "jpegQuantity");
    if (item && cJSON_IsNumber(item)) {
        _config.jpegQuantity = item->valueint;
    }

    item = cJSON_GetObjectItem(doc, "frameSize");
    if (item && cJSON_IsNumber(item)) {
        _config.frameSize = item->valueint;
    }

    cJSON_Delete(doc);
}

void UnitCamS3_5MP::SaveConfig() {
    ESP_LOGI(TAG, "save system config");

    FILE *file = fopen(CONFIG_FILE_PATH, "w");
    if (!file) {
        ESP_LOGI(TAG, "open {%s} failed", CONFIG_FILE_PATH);
        return;
    }

    cJSON *doc = cJSON_CreateObject();

    cJSON_AddStringToObject(doc, "wifiSsid", _config.wifiSsid.c_str());
    cJSON_AddStringToObject(doc, "wifiPass", _config.wifiPass.c_str());
    cJSON_AddStringToObject(doc, "startPoster", _config.startPoster.c_str());
    cJSON_AddStringToObject(doc, "waitApFirst", _config.waitApFirst.c_str());
    cJSON_AddStringToObject(doc, "nickname", _config.nickname.c_str());
    cJSON_AddStringToObject(doc, "timeZone", _config.timeZone.c_str());

    cJSON_AddNumberToObject(doc, "postInterval", _config.postInterval);
    cJSON_AddStringToObject(doc, "postServer", _config.postServer.c_str());
    cJSON_AddNumberToObject(doc, "postPort", _config.postPort);

    cJSON_AddNumberToObject(doc, "jpegQuantity", _config.jpegQuantity);
    cJSON_AddNumberToObject(doc, "frameSize", _config.frameSize);

    char *json_str = cJSON_Print(doc);
    fputs(json_str, file);
    free(json_str);
    cJSON_Delete(doc);
    fclose(file);
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
        ESP_LOGI(TAG, "Failed to get camera frame");
        SetLed(0);
        if (OnTakePhotoEnd != nullptr) {
            OnTakePhotoEnd();
        }
        return;
    }
    
    ESP_LOGI(TAG, "captured height: %d , width: %d, length: %d", fb->height, fb->width, fb->len);
    
    if (processPhoto) {
        try {
            processPhoto(fb);
        } catch (...) {
            ESP_LOGI(TAG, "Exception occurred during photo processing");
        }
    }
    
    esp_camera_fb_return(fb);
    fb = NULL;
    
    if (OnTakePhotoEnd != nullptr) {
        OnTakePhotoEnd();
    }
    
    SetLed(0);
}