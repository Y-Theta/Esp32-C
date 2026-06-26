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
    
    if (fsize <= 0) {
        fclose(file);
        ESP_LOGI(TAG, "Config file is empty, using defaults.");
        return;
    }
    
    char* buffer = (char*)malloc(fsize + 1);
    if (!buffer) {
        fclose(file);
        ESP_LOGE(TAG, "Failed to allocate memory for config file");
        return;
    }
    
    size_t readBytes = fread(buffer, 1, fsize, file);
    buffer[readBytes] = '\0';
    fclose(file);
    
    cJSON* doc = cJSON_Parse(buffer);
    free(buffer);
    
    if (!doc) {
        ESP_LOGE(TAG, "Failed to parse JSON config!");
        return;
    }
    
    cJSON* item = cJSON_GetObjectItem(doc, "wifiSsid");
    if (item && cJSON_IsString(item)) {
        _config.wifiSsid[0] = item->valuestring;
    }
    
    item = cJSON_GetObjectItem(doc, "wifiPass");
    if (item && cJSON_IsString(item)) {
        _config.wifiPass[0] = item->valuestring;
    }

    // 支持多个WiFi配置
    cJSON* wifiList = cJSON_GetObjectItem(doc, "wifiList");
    if (wifiList && cJSON_IsArray(wifiList)) {
        int count = cJSON_GetArraySize(wifiList);
        for (int i = 0; i < count && i < MAX_WIFI_SSIDS; i++) {
            cJSON* wifiItem = cJSON_GetArrayItem(wifiList, i);
            if (wifiItem && cJSON_IsObject(wifiItem)) {
                cJSON* ssid = cJSON_GetObjectItem(wifiItem, "ssid");
                cJSON* pass = cJSON_GetObjectItem(wifiItem, "pass");
                if (ssid && cJSON_IsString(ssid)) _config.wifiSsid[i] = ssid->valuestring;
                if (pass && cJSON_IsString(pass)) _config.wifiPass[i] = pass->valuestring;
            }
        }
    }
    
    item = cJSON_GetObjectItem(doc, "postServer");
    if (item && cJSON_IsString(item)) _config.postServer = item->valuestring;
    
    item = cJSON_GetObjectItem(doc, "postUsePut");
    if (item) {
        if (cJSON_IsBool(item)) _config.postUsePut = cJSON_IsTrue(item);
        else if (cJSON_IsNumber(item)) _config.postUsePut = (item->valueint != 0);
    }
    
    item = cJSON_GetObjectItem(doc, "bootMode");
    if (item && cJSON_IsNumber(item)) _config.bootMode = item->valueint;
    
    item = cJSON_GetObjectItem(doc, "uploadInterval");
    if (item && cJSON_IsNumber(item)) {
        int interval = item->valueint;
        if (interval >= 10 && interval <= 3600) _config.uploadInterval = interval;
    }
    
    item = cJSON_GetObjectItem(doc, "jpegQuantity");
    if (item && cJSON_IsNumber(item)) _config.jpegQuantity = item->valueint;

    item = cJSON_GetObjectItem(doc, "frameSize");
    if (item && cJSON_IsNumber(item)) _config.frameSize = item->valueint;

    item = cJSON_GetObjectItem(doc, "streamFps");
    if (item && cJSON_IsNumber(item)) {
        int fps = item->valueint;
        if (fps >= 15 && fps <= 40) _config.streamFps = fps;
    }

    item = cJSON_GetObjectItem(doc, "streamFrameSize");
    if (item && cJSON_IsNumber(item)) {
        int fs = item->valueint;
        // 推流分辨率限制：96x96(0) ~ VGA(10)
        if (fs >= 0 && fs <= (int)FRAMESIZE_VGA) _config.streamFrameSize = fs;
    }

    item = cJSON_GetObjectItem(doc, "streamFbCount");
    if (item && cJSON_IsNumber(item)) {
        int n = item->valueint;
        if (n >= 1 && n <= 4) _config.streamFbCount = n;
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
    if (!doc) {
        fclose(file);
        ESP_LOGE(TAG, "Failed to create JSON object");
        return;
    }
    
    // 保存多个WiFi配置
    cJSON* wifiList = cJSON_CreateArray();
    for (int i = 0; i < MAX_WIFI_SSIDS; i++) {
        cJSON* wifiItem = cJSON_CreateObject();
        cJSON_AddStringToObject(wifiItem, "ssid", _config.wifiSsid[i].c_str());
        cJSON_AddStringToObject(wifiItem, "pass", _config.wifiPass[i].c_str());
        cJSON_AddItemToArray(wifiList, wifiItem);
    }
    cJSON_AddItemToObject(doc, "wifiList", wifiList);
    // 兼容旧版本
    cJSON_AddStringToObject(doc, "wifiSsid", _config.wifiSsid[0].c_str());
    cJSON_AddStringToObject(doc, "wifiPass", _config.wifiPass[0].c_str());
    
    cJSON_AddStringToObject(doc, "postServer", _config.postServer.c_str());
    cJSON_AddBoolToObject(doc, "postUsePut", _config.postUsePut);
    cJSON_AddNumberToObject(doc, "bootMode", _config.bootMode);
    cJSON_AddNumberToObject(doc, "uploadInterval", _config.uploadInterval);
    
    cJSON_AddNumberToObject(doc, "jpegQuantity", _config.jpegQuantity);
    cJSON_AddNumberToObject(doc, "frameSize", _config.frameSize);
    cJSON_AddNumberToObject(doc, "streamFps", _config.streamFps);
    cJSON_AddNumberToObject(doc, "streamFrameSize", _config.streamFrameSize);
    cJSON_AddNumberToObject(doc, "streamFbCount", _config.streamFbCount);

    cJSON_AddNumberToObject(doc, "wbMode", _config.wbMode);
    cJSON_AddNumberToObject(doc, "contrast", _config.contrast);
    cJSON_AddNumberToObject(doc, "saturation", _config.saturation);
    cJSON_AddNumberToObject(doc, "brightness", _config.brightness);
    cJSON_AddNumberToObject(doc, "specialEffect", _config.specialEffect);

    char* json_str = cJSON_Print(doc);
    if (json_str) {
        fputs(json_str, file);
        free(json_str);
    } else {
        ESP_LOGE(TAG, "Failed to print JSON to string");
    }
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
    _config.wifiSsid[0] = ssid;
    _config.wifiPass[0] = password;
}

void StorageService::setWifiConfig(int index, const std::string& ssid, const std::string& password) {
    if (index >= 0 && index < MAX_WIFI_SSIDS) {
        _config.wifiSsid[index] = ssid;
        _config.wifiPass[index] = password;
    }
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