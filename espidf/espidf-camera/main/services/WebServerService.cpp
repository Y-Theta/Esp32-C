#include "services/WebServerService.h"
#include "services/StorageService.h"
#include "camera/UnitCamS3_5MP.h"
#include "esp_log.h"
#include "cJSON.h"
#include <esp_system.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* TAG = "WebServerService";

// 全局缓存最后一张照片
static uint8_t* _photoBuffer = nullptr;
static size_t _photoBufferSize = 0;

esp_err_t WebServerService::serveFile(httpd_req_t* req, const char* path, const char* contentType) {
    ESP_LOGI(TAG, "Serving file: %s", path);
    
    FILE* file = fopen(path, "rb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file: %s", path);
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_sendstr(req, "File not found");
        return ESP_FAIL;
    }
    
    fseek(file, 0, SEEK_END);
    long fsize = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    uint8_t* buffer = (uint8_t*)malloc(fsize);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate memory for file");
        fclose(file);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "Server out of memory");
        return ESP_FAIL;
    }
    
    fread(buffer, 1, fsize, file);
    fclose(file);
    
    httpd_resp_set_type(req, contentType);
    httpd_resp_send(req, (const char*)buffer, fsize);
    free(buffer);
    
    return ESP_OK;
}

esp_err_t WebServerService::indexHandler(httpd_req_t* req) {
    return serveFile(req, "/spiffs/index.html", "text/html");
}

esp_err_t WebServerService::cssHandler(httpd_req_t* req) {
    return serveFile(req, "/spiffs/style.css", "text/css");
}

esp_err_t WebServerService::jsHandler(httpd_req_t* req) {
    return serveFile(req, "/spiffs/app.js", "application/javascript");
}

esp_err_t WebServerService::getConfigHandler(httpd_req_t* req) {
    ESP_LOGI(TAG, "Getting config");
    
    StorageService& storage = StorageService::getInstance();
    const CONFIG::SystemConfig_t& config = storage.getConfig();
    
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "wifiSsid", config.wifiSsid.c_str());
    cJSON_AddStringToObject(root, "wifiPass", config.wifiPass.c_str());
    cJSON_AddStringToObject(root, "postServer", config.postServer.c_str());
    cJSON_AddNumberToObject(root, "postPort", config.postPort);
    cJSON_AddNumberToObject(root, "postInterval", config.postInterval);
    cJSON_AddNumberToObject(root, "jpegQuantity", config.jpegQuantity);
    cJSON_AddNumberToObject(root, "frameSize", config.frameSize);
    cJSON_AddNumberToObject(root, "wbMode", config.wbMode);
    cJSON_AddNumberToObject(root, "contrast", config.contrast);
    cJSON_AddNumberToObject(root, "saturation", config.saturation);
    cJSON_AddNumberToObject(root, "brightness", config.brightness);
    cJSON_AddNumberToObject(root, "specialEffect", config.specialEffect);
    cJSON_AddStringToObject(root, "startPoster", config.startPoster.c_str());
    cJSON_AddStringToObject(root, "waitApFirst", config.waitApFirst.c_str());
    cJSON_AddStringToObject(root, "nickname", config.nickname.c_str());
    cJSON_AddStringToObject(root, "timeZone", config.timeZone.c_str());

    char* json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    
    free(json_str);
    cJSON_Delete(root);
    
    return ESP_OK;
}

esp_err_t WebServerService::saveConfigHandler(httpd_req_t* req) {
    ESP_LOGI(TAG, "Saving config");
    
    int total_len = req->content_len;
    char* buf = (char*)malloc(total_len + 1);
    if (!buf) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "Memory allocation failed");
        return ESP_FAIL;
    }
    
    int received = 0;
    while (received < total_len) {
        int ret = httpd_req_recv(req, buf + received, total_len - received);
        if (ret <= 0) {
            free(buf);
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_sendstr(req, "Failed to receive data");
            return ESP_FAIL;
        }
        received += ret;
    }
    buf[total_len] = '\0';
    
    cJSON* root = cJSON_Parse(buf);
    free(buf);
    
    if (!root) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "Invalid JSON");
        return ESP_FAIL;
    }
    
    StorageService& storage = StorageService::getInstance();
    CONFIG::SystemConfig_t config = storage.getConfig();
    
    cJSON* item = cJSON_GetObjectItem(root, "wifiSsid");
    if (item && cJSON_IsString(item)) config.wifiSsid = item->valuestring;
    
    item = cJSON_GetObjectItem(root, "wifiPass");
    if (item && cJSON_IsString(item)) config.wifiPass = item->valuestring;
    
    item = cJSON_GetObjectItem(root, "postServer");
    if (item && cJSON_IsString(item)) config.postServer = item->valuestring;
    
    item = cJSON_GetObjectItem(root, "postPort");
    if (item && cJSON_IsNumber(item)) config.postPort = item->valueint;
    
    item = cJSON_GetObjectItem(root, "postInterval");
    if (item && cJSON_IsNumber(item)) config.postInterval = item->valueint;
    
    item = cJSON_GetObjectItem(root, "jpegQuantity");
    if (item && cJSON_IsNumber(item)) config.jpegQuantity = item->valueint;

    item = cJSON_GetObjectItem(root, "frameSize");
    if (item && cJSON_IsNumber(item)) config.frameSize = item->valueint;

    item = cJSON_GetObjectItem(root, "wbMode");
    if (item && cJSON_IsNumber(item)) config.wbMode = item->valueint;

    item = cJSON_GetObjectItem(root, "contrast");
    if (item && cJSON_IsNumber(item)) config.contrast = item->valueint;

    item = cJSON_GetObjectItem(root, "saturation");
    if (item && cJSON_IsNumber(item)) config.saturation = item->valueint;

    item = cJSON_GetObjectItem(root, "brightness");
    if (item && cJSON_IsNumber(item)) config.brightness = item->valueint;

    item = cJSON_GetObjectItem(root, "specialEffect");
    if (item && cJSON_IsNumber(item)) config.specialEffect = item->valueint;

    item = cJSON_GetObjectItem(root, "startPoster");
    if (item && cJSON_IsString(item)) config.startPoster = item->valuestring;

    item = cJSON_GetObjectItem(root, "waitApFirst");
    if (item && cJSON_IsString(item)) config.waitApFirst = item->valuestring;

    item = cJSON_GetObjectItem(root, "nickname");
    if (item && cJSON_IsString(item)) config.nickname = item->valuestring;

    item = cJSON_GetObjectItem(root, "timeZone");
    if (item && cJSON_IsString(item)) config.timeZone = item->valuestring;

    cJSON_Delete(root);
    
    storage.setConfig(config);
    storage.save();
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    
    ESP_LOGI(TAG, "Config saved, restarting...");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    esp_restart();
    
    return ESP_OK;
}

esp_err_t WebServerService::apiTakePhotoHandler(httpd_req_t* req) {
    ESP_LOGI(TAG, "API: Take photo requested");

    UnitCamS3_5MP& camera = UnitCamS3_5MP::getInstance();

    // 使用 lambda 捕获照片
    bool photoTaken = false;
    size_t photoLen = 0;

    camera.WebTakePhoto([&photoTaken, &photoLen](camera_fb_t* fb) {
        if (fb && fb->format == PIXFORMAT_JPEG) {
            ESP_LOGI(TAG, "Photo taken, size: %zu bytes", fb->len);

            // 释放之前的缓存
            if (_photoBuffer) {
                free(_photoBuffer);
                _photoBuffer = nullptr;
            }

            // 分配新缓存
            _photoBuffer = (uint8_t*)malloc(fb->len);
            if (_photoBuffer) {
                memcpy(_photoBuffer, fb->buf, fb->len);
                _photoBufferSize = fb->len;
                photoLen = fb->len;
                photoTaken = true;
            }
        }
    });

    if (photoTaken) {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "success", true);
        cJSON_AddNumberToObject(root, "photoSize", photoLen);
        cJSON_AddStringToObject(root, "photoUrl", "/api/last-photo");

        char* json_str = cJSON_Print(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, json_str);
        free(json_str);
        cJSON_Delete(root);

        return ESP_OK;
    } else {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"success\":false,\"error\":\"Failed to take photo\"}");
        return ESP_FAIL;
    }
}

esp_err_t WebServerService::apiLastPhotoHandler(httpd_req_t* req) {
    ESP_LOGI(TAG, "API: Get last photo");
    
    if (!_photoBuffer || _photoBufferSize == 0) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_sendstr(req, "{\"error\":\"No photo available\"}");
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_send(req, (const char*)_photoBuffer, _photoBufferSize);
    
    return ESP_OK;
}

esp_err_t WebServerService::apiConnectSTAHandler(httpd_req_t* req) {
    ESP_LOGI(TAG, "API: Connect to STA requested");
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true,\"message\":\"Connecting to STA...\"}");
    
    return ESP_OK;
}

esp_err_t WebServerService::apiGetStatusHandler(httpd_req_t* req) {
    ESP_LOGI(TAG, "API: Get status");
    
    StorageService& storage = StorageService::getInstance();
    const CONFIG::SystemConfig_t& config = storage.getConfig();
    
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "wifiConnected", false); 
    cJSON_AddBoolToObject(root, "isAPMode", true); 
    cJSON_AddStringToObject(root, "ssid", config.wifiSsid.c_str());
    
    char* json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    
    free(json_str);
    cJSON_Delete(root);
    
    return ESP_OK;
}

static esp_err_t readPostJson(httpd_req_t* req, cJSON** root) {
    int total_len = req->content_len;
    if (total_len <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"success\":false,\"error\":\"Empty body\"}");
        return ESP_FAIL;
    }

    char* buf = (char*)malloc(total_len + 1);
    if (!buf) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"success\":false,\"error\":\"Memory allocation failed\"}");
        return ESP_FAIL;
    }

    int received = 0;
    while (received < total_len) {
        int ret = httpd_req_recv(req, buf + received, total_len - received);
        if (ret <= 0) {
            free(buf);
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_sendstr(req, "{\"success\":false,\"error\":\"Failed to receive data\"}");
            return ESP_FAIL;
        }
        received += ret;
    }
    buf[total_len] = '\0';

    *root = cJSON_Parse(buf);
    free(buf);

    if (!*root) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"success\":false,\"error\":\"Invalid JSON\"}");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t readPostJsonInt(httpd_req_t* req, const char* key, int* value, const char* errorName) {
    cJSON* root = nullptr;
    if (readPostJson(req, &root) != ESP_OK) {
        return ESP_FAIL;
    }

    cJSON* item = cJSON_GetObjectItem(root, key);
    if (!item || !cJSON_IsNumber(item)) {
        cJSON_Delete(root);
        httpd_resp_set_status(req, "400 Bad Request");
        char errorMsg[64];
        snprintf(errorMsg, sizeof(errorMsg), "{\"success\":false,\"error\":\"%s is required\"}", errorName);
        httpd_resp_sendstr(req, errorMsg);
        return ESP_FAIL;
    }

    *value = item->valueint;
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t WebServerService::apiSetFrameSizeHandler(httpd_req_t* req) {
    ESP_LOGI(TAG, "API: Set frame size");

    int frameSize = 0;
    if (readPostJsonInt(req, "frameSize", &frameSize, "frameSize") != ESP_OK) {
        return ESP_FAIL;
    }

    UnitCamS3_5MP& camera = UnitCamS3_5MP::getInstance();
    camera.SetFrameSize((framesize_t)frameSize);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

esp_err_t WebServerService::apiSetJpegQualityHandler(httpd_req_t* req) {
    ESP_LOGI(TAG, "API: Set JPEG quality");

    int quality = 0;
    if (readPostJsonInt(req, "quality", &quality, "quality") != ESP_OK) {
        return ESP_FAIL;
    }

    UnitCamS3_5MP& camera = UnitCamS3_5MP::getInstance();
    camera.SetJpegQuality(quality);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

esp_err_t WebServerService::apiSetWhiteBalanceHandler(httpd_req_t* req) {
    ESP_LOGI(TAG, "API: Set white balance");

    int wbMode = 0;
    if (readPostJsonInt(req, "wbMode", &wbMode, "wbMode") != ESP_OK) {
        return ESP_FAIL;
    }

    UnitCamS3_5MP& camera = UnitCamS3_5MP::getInstance();
    camera.SetWhiteBalance(wbMode);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

esp_err_t WebServerService::apiSetContrastHandler(httpd_req_t* req) {
    ESP_LOGI(TAG, "API: Set contrast");

    int contrast = 0;
    if (readPostJsonInt(req, "contrast", &contrast, "contrast") != ESP_OK) {
        return ESP_FAIL;
    }

    UnitCamS3_5MP& camera = UnitCamS3_5MP::getInstance();
    camera.SetContrast(contrast);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

esp_err_t WebServerService::apiSetSaturationHandler(httpd_req_t* req) {
    ESP_LOGI(TAG, "API: Set saturation");

    int saturation = 0;
    if (readPostJsonInt(req, "saturation", &saturation, "saturation") != ESP_OK) {
        return ESP_FAIL;
    }

    UnitCamS3_5MP& camera = UnitCamS3_5MP::getInstance();
    camera.SetSaturation(saturation);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

esp_err_t WebServerService::apiSetBrightnessHandler(httpd_req_t* req) {
    ESP_LOGI(TAG, "API: Set brightness");

    int brightness = 0;
    if (readPostJsonInt(req, "brightness", &brightness, "brightness") != ESP_OK) {
        return ESP_FAIL;
    }

    UnitCamS3_5MP& camera = UnitCamS3_5MP::getInstance();
    camera.SetBrightness(brightness);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

esp_err_t WebServerService::apiSetSpecialEffectHandler(httpd_req_t* req) {
    ESP_LOGI(TAG, "API: Set special effect");

    int effect = 0;
    if (readPostJsonInt(req, "effect", &effect, "effect") != ESP_OK) {
        return ESP_FAIL;
    }

    UnitCamS3_5MP& camera = UnitCamS3_5MP::getInstance();
    camera.SetSpecialEffect(effect);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

esp_err_t WebServerService::apiApplyCameraConfigHandler(httpd_req_t* req) {
    ESP_LOGI(TAG, "API: Apply camera config");

    UnitCamS3_5MP& camera = UnitCamS3_5MP::getInstance();
    camera.ApplyCameraConfig();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

esp_err_t WebServerService::apiCameraStatusHandler(httpd_req_t* req) {
    ESP_LOGI(TAG, "API: Get camera status");

    UnitCamS3_5MP& camera = UnitCamS3_5MP::getInstance();
    StorageService& storage = StorageService::getInstance();
    const CONFIG::SystemConfig_t& config = storage.getConfig();

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "initialized", camera.IsInitialized());
    cJSON_AddNumberToObject(root, "frameSize", config.frameSize);
    cJSON_AddNumberToObject(root, "jpegQuantity", config.jpegQuantity);
    cJSON_AddNumberToObject(root, "wbMode", config.wbMode);
    cJSON_AddNumberToObject(root, "contrast", config.contrast);
    cJSON_AddNumberToObject(root, "saturation", config.saturation);
    cJSON_AddNumberToObject(root, "brightness", config.brightness);
    cJSON_AddNumberToObject(root, "specialEffect", config.specialEffect);

    char* json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    free(json_str);
    cJSON_Delete(root);

    return ESP_OK;
}

void WebServerService::start() {
    if (_server) {
        ESP_LOGI(TAG, "Server already running");
        return;
    }
    
    ESP_LOGI(TAG, "Starting web server...");
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.lru_purge_enable = true;
    config.max_open_sockets = 4;
    config.stack_size = 8192;
    config.max_uri_len = 4096;
    config.max_req_hdr_len = 4096;
    config.max_uri_handlers = 32;
    
    if (httpd_start(&_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server");
        return;
    }
    
    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = indexHandler,
        .user_ctx = nullptr
    };
    
    httpd_uri_t css_uri = {
        .uri = "/style.css",
        .method = HTTP_GET,
        .handler = cssHandler,
        .user_ctx = nullptr
    };
    
    httpd_uri_t js_uri = {
        .uri = "/app.js",
        .method = HTTP_GET,
        .handler = jsHandler,
        .user_ctx = nullptr
    };
    
    httpd_uri_t get_config_uri = {
        .uri = "/api/config",
        .method = HTTP_GET,
        .handler = getConfigHandler,
        .user_ctx = nullptr
    };
    
    httpd_uri_t save_config_uri = {
        .uri = "/api/config",
        .method = HTTP_POST,
        .handler = saveConfigHandler,
        .user_ctx = nullptr
    };
    
    httpd_uri_t take_photo_uri = {
        .uri = "/api/take-photo",
        .method = HTTP_POST,
        .handler = apiTakePhotoHandler,
        .user_ctx = nullptr
    };
    
    httpd_uri_t last_photo_uri = {
        .uri = "/api/last-photo",
        .method = HTTP_GET,
        .handler = apiLastPhotoHandler,
        .user_ctx = nullptr
    };
    
    httpd_uri_t connect_sta_uri = {
        .uri = "/api/connect-sta",
        .method = HTTP_POST,
        .handler = apiConnectSTAHandler,
        .user_ctx = nullptr
    };
    
    httpd_uri_t status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = apiGetStatusHandler,
        .user_ctx = nullptr
    };

    httpd_uri_t camera_status_uri = {
        .uri = "/api/camera/status",
        .method = HTTP_GET,
        .handler = apiCameraStatusHandler,
        .user_ctx = nullptr
    };

    httpd_uri_t set_frame_size_uri = {
        .uri = "/api/camera/frame-size",
        .method = HTTP_POST,
        .handler = apiSetFrameSizeHandler,
        .user_ctx = nullptr
    };

    httpd_uri_t set_jpeg_quality_uri = {
        .uri = "/api/camera/quality",
        .method = HTTP_POST,
        .handler = apiSetJpegQualityHandler,
        .user_ctx = nullptr
    };

    httpd_uri_t set_white_balance_uri = {
        .uri = "/api/camera/white-balance",
        .method = HTTP_POST,
        .handler = apiSetWhiteBalanceHandler,
        .user_ctx = nullptr
    };

    httpd_uri_t set_contrast_uri = {
        .uri = "/api/camera/contrast",
        .method = HTTP_POST,
        .handler = apiSetContrastHandler,
        .user_ctx = nullptr
    };

    httpd_uri_t set_saturation_uri = {
        .uri = "/api/camera/saturation",
        .method = HTTP_POST,
        .handler = apiSetSaturationHandler,
        .user_ctx = nullptr
    };

    httpd_uri_t set_brightness_uri = {
        .uri = "/api/camera/brightness",
        .method = HTTP_POST,
        .handler = apiSetBrightnessHandler,
        .user_ctx = nullptr
    };

    httpd_uri_t set_special_effect_uri = {
        .uri = "/api/camera/special-effect",
        .method = HTTP_POST,
        .handler = apiSetSpecialEffectHandler,
        .user_ctx = nullptr
    };

    httpd_uri_t apply_camera_config_uri = {
        .uri = "/api/camera/apply",
        .method = HTTP_POST,
        .handler = apiApplyCameraConfigHandler,
        .user_ctx = nullptr
    };

    httpd_register_uri_handler(_server, &index_uri);
    httpd_register_uri_handler(_server, &css_uri);
    httpd_register_uri_handler(_server, &js_uri);
    httpd_register_uri_handler(_server, &get_config_uri);
    httpd_register_uri_handler(_server, &save_config_uri);
    httpd_register_uri_handler(_server, &take_photo_uri);
    httpd_register_uri_handler(_server, &last_photo_uri);
    httpd_register_uri_handler(_server, &connect_sta_uri);
    httpd_register_uri_handler(_server, &status_uri);
    httpd_register_uri_handler(_server, &camera_status_uri);
    httpd_register_uri_handler(_server, &set_frame_size_uri);
    httpd_register_uri_handler(_server, &set_jpeg_quality_uri);
    httpd_register_uri_handler(_server, &set_white_balance_uri);
    httpd_register_uri_handler(_server, &set_contrast_uri);
    httpd_register_uri_handler(_server, &set_saturation_uri);
    httpd_register_uri_handler(_server, &set_brightness_uri);
    httpd_register_uri_handler(_server, &set_special_effect_uri);
    httpd_register_uri_handler(_server, &apply_camera_config_uri);

    ESP_LOGI(TAG, "Web server started");
}

void WebServerService::stop() {
    if (_server) {
        httpd_stop(_server);
        _server = nullptr;
        ESP_LOGI(TAG, "Web server stopped");
    }
}