#include "services/StorageService.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* TAG = "StorageService";

bool StorageService::initSpiffs() {
    if (_spiffsInitialized) return true;
    
    ESP_LOGI(TAG, "Initializing SPIFFS...");
    
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "spiffs",
        .max_files = 5,
        .format_if_mount_failed = true
    };
    
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return false;
    }
    
    size_t total = 0, used = 0;
    ret = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS: total=%d bytes, used=%d bytes", total, used);
    }
    
    _spiffsInitialized = true;
    ESP_LOGI(TAG, "SPIFFS initialized successfully!");
    return true;
}

void StorageService::init() {
    if (_initialized) return;
    
    ESP_LOGI(TAG, "Initializing StorageService...");
    
    if (!initSpiffs()) {
        ESP_LOGE(TAG, "Failed to initialize SPIFFS, cannot load config!");
        return;
    }
    
    load();
    _initialized = true;
}

void StorageService::load() {
    ESP_LOGI(TAG, "Loading config...");
    
    FILE* file = fopen(CONFIG_FILE_PATH, "r");
    if (!file) {
        ESP_LOGI(TAG, "Config file doesn't exist, using defaults.");
        return;
    }
    
    fseek(file, 0, SEEK_END);
    long fsize = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char* buffer = (char*)malloc(fsize + 1);
    fread(buffer, 1, fsize, file);
    buffer[fsize] = '\0';
    fclose(file);
    
    cJSON* doc = cJSON_Parse(buffer);
    free(buffer);
    
    if (!doc) {
        ESP_LOGE(TAG, "Failed to parse JSON config!");
        return;
    }
    
    cJSON* item = cJSON_GetObjectItem(doc, "wifiSsid");
    if (item && cJSON_IsString(item)) _config.wifiSsid = item->valuestring;
    
    item = cJSON_GetObjectItem(doc, "wifiPass");
    if (item && cJSON_IsString(item)) _config.wifiPass = item->valuestring;
    
    item = cJSON_GetObjectItem(doc, "startPoster");
    if (item && cJSON_IsString(item)) _config.startPoster = item->valuestring;
    
    item = cJSON_GetObjectItem(doc, "waitApFirst");
    if (item && cJSON_IsString(item)) _config.waitApFirst = item->valuestring;
    
    item = cJSON_GetObjectItem(doc, "nickname");
    if (item && cJSON_IsString(item)) _config.nickname = item->valuestring;
    
    item = cJSON_GetObjectItem(doc, "timeZone");
    if (item && cJSON_IsString(item)) _config.timeZone = item->valuestring;
    
    item = cJSON_GetObjectItem(doc, "postInterval");
    if (item && cJSON_IsNumber(item)) _config.postInterval = item->valueint;
    
    item = cJSON_GetObjectItem(doc, "postServer");
    if (item && cJSON_IsString(item)) _config.postServer = item->valuestring;
    
    item = cJSON_GetObjectItem(doc, "postPort");
    if (item && cJSON_IsNumber(item)) _config.postPort = item->valueint;
    
    item = cJSON_GetObjectItem(doc, "jpegQuantity");
    if (item && cJSON_IsNumber(item)) _config.jpegQuantity = item->valueint;

    item = cJSON_GetObjectItem(doc, "frameSize");
    if (item && cJSON_IsNumber(item)) _config.frameSize = item->valueint;

    item = cJSON_GetObjectItem(doc, "streamFps");
    if (item && cJSON_IsNumber(item)) {
        int fps = item->valueint;
        if (fps >= 15 && fps <= 30) _config.streamFps = fps;
    }

    item = cJSON_GetObjectItem(doc, "wbMode");
    if (item && cJSON_IsNumber(item)) _config.wbMode = item->valueint;

    item = cJSON_GetObjectItem(doc, "contrast");
    if (item && cJSON_IsNumber(item)) _config.contrast = item->valueint;

    item = cJSON_GetObjectItem(doc, "saturation");
    if (item && cJSON_IsNumber(item)) _config.saturation = item->valueint;

    item = cJSON_GetObjectItem(doc, "brightness");
    if (item && cJSON_IsNumber(item)) _config.brightness = item->valueint;

    item = cJSON_GetObjectItem(doc, "specialEffect");
    if (item && cJSON_IsNumber(item)) _config.specialEffect = item->valueint;

    cJSON_Delete(doc);
    ESP_LOGI(TAG, "Config loaded successfully!");
}

void StorageService::save() {
    ESP_LOGI(TAG, "Saving config...");
    
    FILE* file = fopen(CONFIG_FILE_PATH, "w");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open config file for writing!");
        return;
    }
    
    cJSON* doc = cJSON_CreateObject();
    
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
    cJSON_AddNumberToObject(doc, "streamFps", _config.streamFps);

    cJSON_AddNumberToObject(doc, "wbMode", _config.wbMode);
    cJSON_AddNumberToObject(doc, "contrast", _config.contrast);
    cJSON_AddNumberToObject(doc, "saturation", _config.saturation);
    cJSON_AddNumberToObject(doc, "brightness", _config.brightness);
    cJSON_AddNumberToObject(doc, "specialEffect", _config.specialEffect);

    char* json_str = cJSON_Print(doc);
    fputs(json_str, file);
    free(json_str);
    cJSON_Delete(doc);
    fclose(file);
    
    ESP_LOGI(TAG, "Config saved successfully!");
}

const CONFIG::SystemConfig_t& StorageService::getConfig() const {
    return _config;
}

void StorageService::setConfig(const CONFIG::SystemConfig_t& config) {
    _config = config;
}

void StorageService::setWifiConfig(const std::string& ssid, const std::string& password) {
    _config.wifiSsid = ssid;
    _config.wifiPass = password;
}

void StorageService::setCameraConfig(int jpegQuality, int frameSize, int wbMode, int contrast, int saturation, int brightness, int specialEffect) {
    _config.jpegQuantity = jpegQuality;
    _config.frameSize = frameSize;
    _config.wbMode = wbMode;
    _config.contrast = contrast;
    _config.saturation = saturation;
    _config.brightness = brightness;
    _config.specialEffect = specialEffect;
}

void StorageService::setPostConfig(const std::string& server, int port, int interval) {
    _config.postServer = server;
    _config.postPort = port;
    _config.postInterval = interval;
}