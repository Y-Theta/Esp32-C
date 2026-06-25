#include "services/WebServerService.h"
#include "services/StorageService.h"
#include "camera/UnitCamS3_5MP.h"
#include "esp_log.h"
#include "cJSON.h"
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* TAG = "WebServerService";

// 全局缓存最后一张照片
static uint8_t* _photoBuffer = nullptr;
static size_t _photoBufferSize = 0;
// 拍照状态标志位
static bool _isTakingPhoto = false;
// 新照片准备就绪标志位
static bool _newPhotoReady = false;
// 照片计数器，用于区分新旧照片
static uint32_t _photoVersion = 0;
// 照片缓冲区最后使用时间戳（毫秒）
static uint64_t _photoBufferLastUseMs = 0;
// 照片缓冲区超时时间（30秒）
static const uint64_t PHOTO_BUFFER_TIMEOUT_MS = 30 * 1000;

// 辅助函数：获取当前时间（毫秒）
static uint64_t getCurrentTimeMs() {
    return (uint64_t)(esp_timer_get_time() / 1000);
}

// 清理过期的资源
void WebServerService::cleanupOldResources() {
    // 只有在拍照或其他非访问路径才清理
    // 检查照片缓冲区是否超时
    if (_photoBuffer && _photoBufferSize > 0 && !_isTakingPhoto) {
        uint64_t now = getCurrentTimeMs();
        if (now - _photoBufferLastUseMs > PHOTO_BUFFER_TIMEOUT_MS) {
            ESP_LOGI(TAG, "Photo buffer timeout, releasing %zu bytes", _photoBufferSize);
            free(_photoBuffer);
            _photoBuffer = nullptr;
            _photoBufferSize = 0;
            _newPhotoReady = false;
        }
    }
}

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
    
    httpd_resp_set_type(req, contentType);
    
    const size_t chunk_size = 4096;
    uint8_t* buffer = (uint8_t*)malloc(chunk_size);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate memory for chunk");
        fclose(file);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "Server out of memory");
        return ESP_FAIL;
    }
    
    bool send_success = true;
    size_t remaining = fsize;
    while (remaining > 0) {
        size_t to_read = remaining < chunk_size ? remaining : chunk_size;
        size_t read = fread(buffer, 1, to_read, file);
        if (read != to_read) {
            ESP_LOGE(TAG, "Failed to read file");
            send_success = false;
            break;
        }
        
        esp_err_t ret = httpd_resp_send_chunk(req, (const char*)buffer, read);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send chunk");
            send_success = false;
            break;
        }
        
        remaining -= read;
    }
    
    // 清理资源 - 无论成功或失败
    fclose(file);
    free(buffer);
    
    if (send_success) {
        httpd_resp_send_chunk(req, nullptr, 0);
    }
    
    return send_success ? ESP_OK : ESP_FAIL;
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
    
    // 立即强制清理旧缓存，不给任何机会
    ESP_LOGI(TAG, "Clearing old photo buffer before taking new one");
    if (_photoBuffer) {
        free(_photoBuffer);
        _photoBuffer = nullptr;
        _photoBufferSize = 0;
    }
    _newPhotoReady = false;
    _isTakingPhoto = true;

    UnitCamS3_5MP& camera = UnitCamS3_5MP::getInstance();

    // 使用 lambda 捕获照片
    bool photoTaken = false;
    size_t photoLen = 0;

    camera.WebTakePhoto([&photoTaken, &photoLen](camera_fb_t* fb) {
        if (fb && fb->format == PIXFORMAT_JPEG) {
            ESP_LOGI(TAG, "Photo taken, size: %zu bytes", fb->len);

            // 再次确认清理（双重保险）
            if (_photoBuffer) {
                free(_photoBuffer);
                _photoBuffer = nullptr;
                _photoBufferSize = 0;
            }

            // 分配新缓存
            _photoBuffer = (uint8_t*)malloc(fb->len);
            if (_photoBuffer) {
                memcpy(_photoBuffer, fb->buf, fb->len);
                _photoBufferSize = fb->len;
                photoLen = fb->len;
                photoTaken = true;
                _photoVersion++;
                _newPhotoReady = true;
                _photoBufferLastUseMs = getCurrentTimeMs();
                ESP_LOGI(TAG, "New photo buffered, version: %u, size: %zu bytes", _photoVersion, fb->len);
            } else {
                ESP_LOGE(TAG, "Failed to allocate buffer for new photo");
            }
        }
        _isTakingPhoto = false;
    });

    if (photoTaken) {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "success", true);
        cJSON_AddNumberToObject(root, "photoSize", photoLen);
        cJSON_AddNumberToObject(root, "photoVersion", _photoVersion);
        cJSON_AddStringToObject(root, "photoUrl", "/api/last-photo");

        char* json_str = cJSON_Print(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, json_str);
        free(json_str);
        cJSON_Delete(root);

        ESP_LOGI(TAG, "Take photo API response sent");
        return ESP_OK;
    } else {
        _isTakingPhoto = false;
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"success\":false,\"error\":\"Failed to take photo\"}");
        return ESP_FAIL;
    }
}

esp_err_t WebServerService::apiPhotoStatusHandler(httpd_req_t* req) {
    ESP_LOGI(TAG, "API: Get photo status");
    
    // 清理旧资源
    cleanupOldResources();

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "isTakingPhoto", _isTakingPhoto);
    cJSON_AddBoolToObject(root, "newPhotoReady", _newPhotoReady);
    cJSON_AddNumberToObject(root, "photoVersion", _photoVersion);

    char* json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    free(json_str);
    cJSON_Delete(root);

    return ESP_OK;
}

esp_err_t WebServerService::apiMemoryStatusHandler(httpd_req_t* req) {
    // 清理旧资源
    cleanupOldResources();

    // 获取 RAM 信息（内部 SRAM）
    size_t ramTotal = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    size_t ramFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t ramLargestFree = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);

    // 获取 PSRAM 信息
    size_t psramTotal = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t psramLargestFree = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

    cJSON* root = cJSON_CreateObject();
    cJSON* ram = cJSON_CreateObject();
    cJSON_AddNumberToObject(ram, "total", ramTotal);
    cJSON_AddNumberToObject(ram, "used", ramTotal - ramFree);
    cJSON_AddNumberToObject(ram, "free", ramFree);
    cJSON_AddNumberToObject(ram, "largestFree", ramLargestFree);
    cJSON_AddItemToObject(root, "ram", ram);

    cJSON* psram = cJSON_CreateObject();
    cJSON_AddNumberToObject(psram, "total", psramTotal);
    cJSON_AddNumberToObject(psram, "used", psramTotal - psramFree);
    cJSON_AddNumberToObject(psram, "free", psramFree);
    cJSON_AddNumberToObject(psram, "largestFree", psramLargestFree);
    cJSON_AddItemToObject(root, "psram", psram);

    char* json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    free(json_str);
    cJSON_Delete(root);

    return ESP_OK;
}

esp_err_t WebServerService::apiLastPhotoHandler(httpd_req_t* req) {
    ESP_LOGI(TAG, "API: Get last photo");
    
    // 更新访问时间 - 表示正在使用
    if (_photoBuffer && _photoBufferSize > 0) {
        _photoBufferLastUseMs = getCurrentTimeMs();
    }
    
    if (!_photoBuffer || _photoBufferSize == 0) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_sendstr(req, "{\"error\":\"No photo available\"}");
        return ESP_FAIL;
    }
    
    // 设置缓存控制头，禁止浏览器缓存
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    
    const size_t chunk_size = 4096;
    size_t offset = 0;
    bool send_success = true;
    
    while (offset < _photoBufferSize) {
        size_t chunk_len = (_photoBufferSize - offset) > chunk_size ? chunk_size : (_photoBufferSize - offset);
        esp_err_t ret = httpd_resp_send_chunk(req, (const char*)(_photoBuffer + offset), chunk_len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send photo chunk at offset %zu", offset);
            send_success = false;
            break;
        }
        offset += chunk_len;
    }
    
    if (send_success) {
        httpd_resp_send_chunk(req, nullptr, 0);
    }
    
    // 关键修复：不要在这里立即释放 buffer！
    // 原因：浏览器可能因为重试、预加载、连接重置等发起多次请求
    // 如果第一次释放了，第二次请求会 404，浏览器就会显示缓存的旧图片
    // 改为依赖超时机制（5分钟后自动清理）来释放
    ESP_LOGI(TAG, "Photo sent successfully (%zu bytes), buffer kept for retry safety", _photoBufferSize);
    
    return send_success ? ESP_OK : ESP_FAIL;
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

esp_err_t WebServerService::apiSetAllCameraConfigHandler(httpd_req_t* req) {
    ESP_LOGI(TAG, "API: Set all camera config");

    cJSON* root = nullptr;
    esp_err_t err = readPostJson(req, &root);
    if (err != ESP_OK) {
        return ESP_FAIL;
    }

    // 解析所有参数
    cJSON* frameSizeJson = cJSON_GetObjectItem(root, "frameSize");
    cJSON* qualityJson = cJSON_GetObjectItem(root, "jpegQuality");
    cJSON* wbModeJson = cJSON_GetObjectItem(root, "wbMode");
    cJSON* specialEffectJson = cJSON_GetObjectItem(root, "specialEffect");
    cJSON* contrastJson = cJSON_GetObjectItem(root, "contrast");
    cJSON* saturationJson = cJSON_GetObjectItem(root, "saturation");
    cJSON* brightnessJson = cJSON_GetObjectItem(root, "brightness");

    // 检查所有必需的字段
    if (!frameSizeJson || !cJSON_IsNumber(frameSizeJson) ||
        !qualityJson || !cJSON_IsNumber(qualityJson) ||
        !wbModeJson || !cJSON_IsNumber(wbModeJson) ||
        !specialEffectJson || !cJSON_IsNumber(specialEffectJson) ||
        !contrastJson || !cJSON_IsNumber(contrastJson) ||
        !saturationJson || !cJSON_IsNumber(saturationJson) ||
        !brightnessJson || !cJSON_IsNumber(brightnessJson)) {
        
        cJSON_Delete(root);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"success\":false,\"error\":\"Invalid parameters\"}");
        return ESP_FAIL;
    }

    int frameSize = frameSizeJson->valueint;
    int jpegQuality = qualityJson->valueint;
    int wbMode = wbModeJson->valueint;
    int specialEffect = specialEffectJson->valueint;
    int contrast = contrastJson->valueint;
    int saturation = saturationJson->valueint;
    int brightness = brightnessJson->valueint;

    cJSON_Delete(root);

    // 应用所有配置
    UnitCamS3_5MP& camera = UnitCamS3_5MP::getInstance();
    camera.SetAllCameraConfig(frameSize, jpegQuality, wbMode, specialEffect, contrast, saturation, brightness);

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

    httpd_uri_t photo_status_uri = {
        .uri = "/api/photo-status",
        .method = HTTP_GET,
        .handler = apiPhotoStatusHandler,
        .user_ctx = nullptr
    };

    httpd_uri_t memory_status_uri = {
        .uri = "/api/memory-status",
        .method = HTTP_GET,
        .handler = apiMemoryStatusHandler,
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

    httpd_uri_t set_all_camera_config_uri = {
        .uri = "/api/camera/set-all",
        .method = HTTP_POST,
        .handler = apiSetAllCameraConfigHandler,
        .user_ctx = nullptr
    };

    httpd_register_uri_handler(_server, &index_uri);
    httpd_register_uri_handler(_server, &css_uri);
    httpd_register_uri_handler(_server, &js_uri);
    httpd_register_uri_handler(_server, &get_config_uri);
    httpd_register_uri_handler(_server, &save_config_uri);
    httpd_register_uri_handler(_server, &take_photo_uri);
    httpd_register_uri_handler(_server, &last_photo_uri);
    httpd_register_uri_handler(_server, &photo_status_uri);
    httpd_register_uri_handler(_server, &memory_status_uri);
    httpd_register_uri_handler(_server, &connect_sta_uri);
    httpd_register_uri_handler(_server, &status_uri);
    httpd_register_uri_handler(_server, &camera_status_uri);
    httpd_register_uri_handler(_server, &set_all_camera_config_uri);

    ESP_LOGI(TAG, "Web server started");
}

void WebServerService::stop() {
    if (_server) {
        httpd_stop(_server);
        _server = nullptr;
        ESP_LOGI(TAG, "Web server stopped");
    }
}