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

// 推流状态
static bool _streamingActive = false;

// ===== 全局静态 URI handler 定义 =====
// 必须放在文件作用域，生命周期永久有效
// httpd_register_uri_handler 只保存指针，不复制内容
static const httpd_uri_t uri_index = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = WebServerService::indexHandler,
    .user_ctx = nullptr
};

static const httpd_uri_t uri_css = {
    .uri = "/style.css",
    .method = HTTP_GET,
    .handler = WebServerService::cssHandler,
    .user_ctx = nullptr
};

static const httpd_uri_t uri_js = {
    .uri = "/app.js",
    .method = HTTP_GET,
    .handler = WebServerService::jsHandler,
    .user_ctx = nullptr
};

static const httpd_uri_t uri_get_config = {
    .uri = "/api/config",
    .method = HTTP_GET,
    .handler = WebServerService::getConfigHandler,
    .user_ctx = nullptr
};

static const httpd_uri_t uri_save_config = {
    .uri = "/api/config",
    .method = HTTP_POST,
    .handler = WebServerService::saveConfigHandler,
    .user_ctx = nullptr
};

static const httpd_uri_t uri_take_photo = {
    .uri = "/api/take-photo",
    .method = HTTP_POST,
    .handler = WebServerService::apiTakePhotoHandler,
    .user_ctx = nullptr
};

static const httpd_uri_t uri_connect_sta = {
    .uri = "/api/connect-sta",
    .method = HTTP_POST,
    .handler = WebServerService::apiConnectSTAHandler,
    .user_ctx = nullptr
};

static const httpd_uri_t uri_get_status = {
    .uri = "/api/status",
    .method = HTTP_GET,
    .handler = WebServerService::apiGetStatusHandler,
    .user_ctx = nullptr
};

static const httpd_uri_t uri_last_photo = {
    .uri = "/api/last-photo",
    .method = HTTP_GET,
    .handler = WebServerService::apiLastPhotoHandler,
    .user_ctx = nullptr
};

static const httpd_uri_t uri_photo_status = {
    .uri = "/api/photo-status",
    .method = HTTP_GET,
    .handler = WebServerService::apiPhotoStatusHandler,
    .user_ctx = nullptr
};

static const httpd_uri_t uri_memory_status = {
    .uri = "/api/memory-status",
    .method = HTTP_GET,
    .handler = WebServerService::apiMemoryStatusHandler,
    .user_ctx = nullptr
};

static const httpd_uri_t uri_set_all_camera_config = {
    .uri = "/api/camera/set-all",
    .method = HTTP_POST,
    .handler = WebServerService::apiSetAllCameraConfigHandler,
    .user_ctx = nullptr
};

static const httpd_uri_t uri_camera_status = {
    .uri = "/api/camera/status",
    .method = HTTP_GET,
    .handler = WebServerService::apiCameraStatusHandler,
    .user_ctx = nullptr
};

static const httpd_uri_t uri_stream_start = {
    .uri = "/api/camera/stream-start",
    .method = HTTP_POST,
    .handler = WebServerService::apiStreamStartHandler,
    .user_ctx = nullptr
};

static const httpd_uri_t uri_stream_stop = {
    .uri = "/api/camera/stream-stop",
    .method = HTTP_POST,
    .handler = WebServerService::apiStreamStopHandler,
    .user_ctx = nullptr
};

static const httpd_uri_t uri_stream_frame = {
    .uri = "/api/camera/stream-frame",
    .method = HTTP_GET,
    .handler = WebServerService::apiStreamFrameHandler,
    .user_ctx = nullptr
};

// 辅助函数：获取当前时间（毫秒）
static uint64_t getCurrentTimeMs() {
    return (uint64_t)(esp_timer_get_time() / 1000);
}

// 清理过期的照片缓存
static void cleanupOldResources() {
    if (_photoBuffer && _photoBufferSize > 0 && !_isTakingPhoto && !_streamingActive) {
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

// 辅助函数：读取 POST JSON 数据
static esp_err_t readPostJson(httpd_req_t* req, cJSON** outRoot) {
    int totalLen = req->content_len;
    if (totalLen <= 0) {
        return ESP_FAIL;
    }

    char* buf = (char*)malloc(totalLen + 1);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }

    int received = 0;
    while (received < totalLen) {
        int ret = httpd_req_recv(req, buf + received, totalLen - received);
        if (ret <= 0) {
            free(buf);
            return ESP_FAIL;
        }
        received += ret;
    }
    buf[totalLen] = '\0';

    *outRoot = cJSON_Parse(buf);
    free(buf);

    if (!*outRoot) {
        return ESP_FAIL;
    }
    return ESP_OK;
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

    httpd_resp_set_type(req, contentType);
    
    char buf[1024];
    size_t readBytes;
    while ((readBytes = fread(buf, 1, sizeof(buf), file)) > 0) {
        httpd_resp_send_chunk(req, buf, readBytes);
    }
    
    fclose(file);
    httpd_resp_send_chunk(req, nullptr, 0);
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

// ===== API Handlers =====

esp_err_t WebServerService::getConfigHandler(httpd_req_t* req) {
    cleanupOldResources();
    
    StorageService& storage = StorageService::getInstance();
    const CONFIG::SystemConfig_t& config = storage.getConfig();

    cJSON* root = cJSON_CreateObject();
    
    cJSON_AddStringToObject(root, "wifiSsid", config.wifiSsid.c_str());
    cJSON_AddStringToObject(root, "wifiPass", config.wifiPass.c_str());
    cJSON_AddStringToObject(root, "postServer", config.postServer.c_str());
    cJSON_AddNumberToObject(root, "postPort", config.postPort);
    cJSON_AddNumberToObject(root, "postInterval", config.postInterval);
    cJSON_AddStringToObject(root, "startPoster", config.startPoster.c_str());
    cJSON_AddStringToObject(root, "waitApFirst", config.waitApFirst.c_str());
    cJSON_AddStringToObject(root, "nickname", config.nickname.c_str());
    cJSON_AddStringToObject(root, "timeZone", config.timeZone.c_str());
    
    cJSON_AddNumberToObject(root, "jpegQuantity", config.jpegQuantity);
    cJSON_AddNumberToObject(root, "frameSize", config.frameSize);
    cJSON_AddNumberToObject(root, "streamFps", config.streamFps);
    cJSON_AddNumberToObject(root, "streamFrameSize", config.streamFrameSize);
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

esp_err_t WebServerService::saveConfigHandler(httpd_req_t* req) {
    cJSON* root;
    esp_err_t err = readPostJson(req, &root);
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"success\":false}");
        return ESP_FAIL;
    }

    StorageService& storage = StorageService::getInstance();
    CONFIG::SystemConfig_t config = storage.getConfig();

    cJSON* item;
    
    item = cJSON_GetObjectItem(root, "wifiSsid");
    if (item && cJSON_IsString(item)) config.wifiSsid = item->valuestring;
    
    item = cJSON_GetObjectItem(root, "wifiPass");
    if (item && cJSON_IsString(item)) config.wifiPass = item->valuestring;
    
    item = cJSON_GetObjectItem(root, "postServer");
    if (item && cJSON_IsString(item)) config.postServer = item->valuestring;
    
    item = cJSON_GetObjectItem(root, "postPort");
    if (item && cJSON_IsNumber(item)) config.postPort = item->valueint;
    
    item = cJSON_GetObjectItem(root, "postInterval");
    if (item && cJSON_IsNumber(item)) config.postInterval = item->valueint;
    
    item = cJSON_GetObjectItem(root, "nickname");
    if (item && cJSON_IsString(item)) config.nickname = item->valuestring;
    
    item = cJSON_GetObjectItem(root, "timeZone");
    if (item && cJSON_IsString(item)) config.timeZone = item->valuestring;

    item = cJSON_GetObjectItem(root, "jpegQuantity");
    if (item && cJSON_IsNumber(item)) config.jpegQuantity = item->valueint;
    
    item = cJSON_GetObjectItem(root, "frameSize");
    if (item && cJSON_IsNumber(item)) config.frameSize = item->valueint;
    
    item = cJSON_GetObjectItem(root, "streamFps");
    if (item && cJSON_IsNumber(item)) {
        int fps = item->valueint;
        if (fps >= 15 && fps <= 40) config.streamFps = fps;
    }

    item = cJSON_GetObjectItem(root, "streamFrameSize");
    if (item && cJSON_IsNumber(item)) {
        int fs = item->valueint;
        if (fs >= 0 && fs <= (int)FRAMESIZE_VGA) config.streamFrameSize = fs;
    }
    
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

    storage.setConfig(config);
    storage.save();

    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    
    // 延迟重启
    return ESP_OK;
}

esp_err_t WebServerService::apiTakePhotoHandler(httpd_req_t* req) {
    if (_isTakingPhoto) {
        httpd_resp_set_status(req, "429 Too Many Requests");
        httpd_resp_sendstr(req, "{\"success\":false,\"error\":\"Already taking photo\"}");
        return ESP_FAIL;
    }

    _isTakingPhoto = true;
    _newPhotoReady = false;

    if (WebServerService::getInstance().onTakePhotoRequested) {
        WebServerService::getInstance().onTakePhotoRequested();
    }

    // 拍照会在后台完成，立即返回版本号
    _photoVersion++;
    
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddNumberToObject(root, "photoVersion", _photoVersion);
    cJSON_AddStringToObject(root, "photoUrl", "/api/last-photo");
    cJSON_AddNumberToObject(root, "photoSize", (int)_photoBufferSize);

    char* json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t WebServerService::apiConnectSTAHandler(httpd_req_t* req) {
    if (WebServerService::getInstance().onConnectToSTRequested) {
        WebServerService::getInstance().onConnectToSTRequested();
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

esp_err_t WebServerService::apiGetStatusHandler(httpd_req_t* req) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "isTakingPhoto", _isTakingPhoto);
    cJSON_AddBoolToObject(root, "newPhotoReady", _newPhotoReady);
    cJSON_AddNumberToObject(root, "photoVersion", _photoVersion);
    cJSON_AddBoolToObject(root, "streamingActive", _streamingActive);

    char* json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t WebServerService::apiLastPhotoHandler(httpd_req_t* req) {
    if (!_photoBuffer || _photoBufferSize == 0) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_sendstr(req, "No photo available");
        return ESP_FAIL;
    }

    _photoBufferLastUseMs = getCurrentTimeMs();
    
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_send(req, (const char*)_photoBuffer, _photoBufferSize);
    
    return ESP_OK;
}

esp_err_t WebServerService::apiPhotoStatusHandler(httpd_req_t* req) {
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
    multi_heap_info_t ram_info;
    heap_caps_get_info(&ram_info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    
    multi_heap_info_t psram_info;
    heap_caps_get_info(&psram_info, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    cJSON* root = cJSON_CreateObject();
    
    cJSON* ram = cJSON_CreateObject();
    cJSON_AddNumberToObject(ram, "total", ram_info.total_free_bytes + ram_info.total_allocated_bytes);
    cJSON_AddNumberToObject(ram, "used", ram_info.total_allocated_bytes);
    cJSON_AddNumberToObject(ram, "free", ram_info.total_free_bytes);
    cJSON_AddItemToObject(root, "ram", ram);
    
    cJSON* psram = cJSON_CreateObject();
    cJSON_AddNumberToObject(psram, "total", psram_info.total_free_bytes + psram_info.total_allocated_bytes);
    cJSON_AddNumberToObject(psram, "used", psram_info.total_allocated_bytes);
    cJSON_AddNumberToObject(psram, "free", psram_info.total_free_bytes);
    cJSON_AddItemToObject(root, "psram", psram);

    char* json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t WebServerService::apiSetAllCameraConfigHandler(httpd_req_t* req) {
    // 拍照进行中禁止重载相机，避免 deinit 与 TakePhoto 持有的 fb 冲突导致崩溃
    if (_isTakingPhoto) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_sendstr(req, "{\"success\":false,\"error\":\"Camera busy (taking photo)\"}");
        return ESP_FAIL;
    }

    cJSON* root;
    esp_err_t err = readPostJson(req, &root);
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"success\":false,\"error\":\"Invalid JSON\"}");
        return ESP_FAIL;
    }

    cJSON* frameSizeJson = cJSON_GetObjectItem(root, "frameSize");
    cJSON* qualityJson = cJSON_GetObjectItem(root, "jpegQuality");
    cJSON* wbModeJson = cJSON_GetObjectItem(root, "wbMode");
    cJSON* specialEffectJson = cJSON_GetObjectItem(root, "specialEffect");
    cJSON* contrastJson = cJSON_GetObjectItem(root, "contrast");
    cJSON* saturationJson = cJSON_GetObjectItem(root, "saturation");
    cJSON* brightnessJson = cJSON_GetObjectItem(root, "brightness");
    cJSON* streamFpsJson = cJSON_GetObjectItem(root, "streamFps");
    cJSON* streamFrameSizeJson = cJSON_GetObjectItem(root, "streamFrameSize");

    int frameSize = 10;
    int jpegQuality = 1;
    int wbMode = 0;
    int specialEffect = 0;
    int contrast = 3;
    int saturation = 3;
    int brightness = 4;
    int streamFps = 20;
    int streamFrameSize = (int)FRAMESIZE_VGA;

    if (frameSizeJson && cJSON_IsNumber(frameSizeJson)) frameSize = frameSizeJson->valueint;
    if (qualityJson && cJSON_IsNumber(qualityJson)) jpegQuality = qualityJson->valueint;
    if (wbModeJson && cJSON_IsNumber(wbModeJson)) wbMode = wbModeJson->valueint;
    if (specialEffectJson && cJSON_IsNumber(specialEffectJson)) specialEffect = specialEffectJson->valueint;
    if (contrastJson && cJSON_IsNumber(contrastJson)) contrast = contrastJson->valueint;
    if (saturationJson && cJSON_IsNumber(saturationJson)) saturation = saturationJson->valueint;
    if (brightnessJson && cJSON_IsNumber(brightnessJson)) brightness = brightnessJson->valueint;
    if (streamFpsJson && cJSON_IsNumber(streamFpsJson)) {
        streamFps = streamFpsJson->valueint;
        if (streamFps < 15) streamFps = 15;
        if (streamFps > 40) streamFps = 40;
    }
    if (streamFrameSizeJson && cJSON_IsNumber(streamFrameSizeJson)) {
        streamFrameSize = streamFrameSizeJson->valueint;
        if (streamFrameSize < 0) streamFrameSize = 0;
        if (streamFrameSize > (int)FRAMESIZE_VGA) streamFrameSize = (int)FRAMESIZE_VGA;
    }

    cJSON_Delete(root);

    // 保存到配置
    StorageService& storage = StorageService::getInstance();
    CONFIG::SystemConfig_t config = storage.getConfig();
    config.frameSize = frameSize;
    config.jpegQuantity = jpegQuality;
    config.wbMode = wbMode;
    config.specialEffect = specialEffect;
    config.contrast = contrast;
    config.saturation = saturation;
    config.brightness = brightness;
    config.streamFps = streamFps;
    config.streamFrameSize = streamFrameSize;
    storage.setConfig(config);
    storage.save();

    // 如果不在推流状态，直接应用相机设置
    if (!_streamingActive) {
        UnitCamS3_5MP& camera = UnitCamS3_5MP::getInstance();
        camera.SetAllCameraConfig(frameSize, jpegQuality, wbMode, specialEffect, contrast, saturation, brightness);
        // 检查相机是否成功初始化，避免后续 handler 访问未初始化相机导致崩溃
        if (!camera.IsInitialized()) {
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, "{\"success\":false,\"error\":\"Camera init failed (memory?)\"}");
            return ESP_FAIL;
        }
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

esp_err_t WebServerService::apiCameraStatusHandler(httpd_req_t* req) {
    UnitCamS3_5MP& camera = UnitCamS3_5MP::getInstance();
    StorageService& storage = StorageService::getInstance();
    const CONFIG::SystemConfig_t& config = storage.getConfig();

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "initialized", camera.IsInitialized());
    cJSON_AddNumberToObject(root, "frameSize", config.frameSize);
    cJSON_AddNumberToObject(root, "jpegQuantity", config.jpegQuantity);
    cJSON_AddNumberToObject(root, "streamFps", config.streamFps);
    cJSON_AddNumberToObject(root, "streamFrameSize", config.streamFrameSize);
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

// ===== 新的简单HTTP推流接口 =====

// 启动推流：进入相机流模式
esp_err_t WebServerService::apiStreamStartHandler(httpd_req_t* req) {
    ESP_LOGI(TAG, "API: Start streaming mode");

    if (_streamingActive) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"success\":true,\"alreadyRunning\":true}");
        return ESP_OK;
    }

    // 清理拍照缓存，释放内存
    if (_photoBuffer && _photoBufferSize > 0) {
        ESP_LOGI(TAG, "Cleaning photo buffer (%zu bytes) before streaming", _photoBufferSize);
        free(_photoBuffer);
        _photoBuffer = nullptr;
        _photoBufferSize = 0;
        _newPhotoReady = false;
        _isTakingPhoto = false;
    }

    UnitCamS3_5MP& camera = UnitCamS3_5MP::getInstance();
    camera.StartStreamingMode();
    
    _streamingActive = true;

    StorageService& storage = StorageService::getInstance();
    const CONFIG::SystemConfig_t& config = storage.getConfig();
    
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddNumberToObject(root, "streamFrameSize", config.streamFrameSize);
    cJSON_AddNumberToObject(root, "targetFps", config.streamFps);
    cJSON_AddNumberToObject(root, "frameIntervalMs", 1000 / config.streamFps);

    char* json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// 停止推流：退出相机流模式
esp_err_t WebServerService::apiStreamStopHandler(httpd_req_t* req) {
    ESP_LOGI(TAG, "API: Stop streaming mode");

    if (!_streamingActive) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"success\":true,\"notRunning\":true}");
        return ESP_OK;
    }

    UnitCamS3_5MP& camera = UnitCamS3_5MP::getInstance();
    camera.StopStreamingMode();
    
    _streamingActive = false;

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

// 获取一帧JPEG图像，前端定时调用
esp_err_t WebServerService::apiStreamFrameHandler(httpd_req_t* req) {
    if (!_streamingActive) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"error\":\"Streaming not active\"}");
        return ESP_FAIL;
    }

    UnitCamS3_5MP& camera = UnitCamS3_5MP::getInstance();
    if (!camera.IsInitialized()) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_sendstr(req, "{\"error\":\"Camera not initialized\"}");
        return ESP_FAIL;
    }

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_sendstr(req, "{\"error\":\"Failed to get frame\"}");
        return ESP_FAIL;
    }

    if (fb->format != PIXFORMAT_JPEG) {
        esp_camera_fb_return(fb);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"error\":\"Frame not JPEG\"}");
        return ESP_FAIL;
    }

    // 设置响应头
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    // 启用 keep-alive：复用 TCP 连接，避免每次帧请求都重新握手（节省 ~10-30ms/帧）
    // 必须有正确的 Content-Length，否则会退化为 chunked 编码导致连接关闭
    httpd_resp_set_hdr(req, "Connection", "keep-alive");

    // 附加帧时间戳（开机后毫秒，单调递增），前端按此排序以消除并发请求导致的乱序
    char ts_str[32];
    snprintf(ts_str, sizeof(ts_str), "%llu", (unsigned long long)getCurrentTimeMs());
    httpd_resp_set_hdr(req, "X-Frame-Timestamp", ts_str);

    // 设置 Content-Length 后用 httpd_resp_send 一次发送
    // httpd 内部会使用 send() 直接发送，避免分段 chunk 编码开销
    char content_len[16];
    snprintf(content_len, sizeof(content_len), "%d", (int)fb->len);
    httpd_resp_set_hdr(req, "Content-Length", content_len);

    esp_err_t ret = httpd_resp_send(req, (const char*)fb->buf, fb->len);

    esp_camera_fb_return(fb);
    return ret;
}

// 拍照完成回调，由相机调用
void WebServerService::notifyPhotoCaptured(camera_fb_t* fb) {
    if (!fb) return;

    // 释放旧缓冲区
    if (_photoBuffer) {
        free(_photoBuffer);
        _photoBuffer = nullptr;
    }

    // 复制照片到全局缓冲区
    _photoBuffer = (uint8_t*)malloc(fb->len);
    if (_photoBuffer) {
        memcpy(_photoBuffer, fb->buf, fb->len);
        _photoBufferSize = fb->len;
        _photoBufferLastUseMs = getCurrentTimeMs();
        _newPhotoReady = true;
        ESP_LOGI(TAG, "New photo buffered, version: %u, size: %zu bytes", _photoVersion, fb->len);
    }

    _isTakingPhoto = false;
}

// 释放照片缓冲区（在相机重新初始化前调用）
void WebServerService::releasePhotoBuffer() {
    if (_photoBuffer && _photoBufferSize > 0) {
        ESP_LOGI(TAG, "Releasing photo buffer (%zu bytes) before camera reload", _photoBufferSize);
        free(_photoBuffer);
        _photoBuffer = nullptr;
        _photoBufferSize = 0;
        _newPhotoReady = false;
    }
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
    // LWIP_MAX_SOCKETS=10，HTTP服务器内部占用3个socket，故 max_open_sockets 上限=7
    // 分配：并发帧请求(2) + 内存/拍照状态轮询(2) + 浏览器其他请求(3) = 7
    config.max_open_sockets = 7;
    config.stack_size = 8192;
    // ESP-IDF 默认 max_uri_handlers=8，本服务有16个URI，必须调大！
    // 否则超出数量的URI会静默注册失败，访问时返回404
    config.max_uri_handlers = 20;

    esp_err_t start_err = httpd_start(&_server, &config);
    if (start_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server: %s", esp_err_to_name(start_err));
        return;
    }
    ESP_LOGI(TAG, "Web server socket created, registering URI handlers...");

    // 注册URI handler，检查返回值以防注册失败
    auto regUri = [this](const httpd_uri_t* uri, const char* name) {
        esp_err_t ret = httpd_register_uri_handler(_server, uri);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register URI '%s': %s", name, esp_err_to_name(ret));
        }
    };

    regUri(&uri_index, "/");
    regUri(&uri_css, "/style.css");
    regUri(&uri_js, "/app.js");
    regUri(&uri_get_config, "GET /api/config");
    regUri(&uri_save_config, "POST /api/config");
    regUri(&uri_take_photo, "/api/take-photo");
    regUri(&uri_connect_sta, "/api/connect-sta");
    regUri(&uri_get_status, "/api/status");
    regUri(&uri_last_photo, "/api/last-photo");
    regUri(&uri_photo_status, "/api/photo-status");
    regUri(&uri_memory_status, "/api/memory-status");
    regUri(&uri_set_all_camera_config, "/api/camera/set-all");
    regUri(&uri_camera_status, "/api/camera/status");
    regUri(&uri_stream_start, "/api/camera/stream-start");
    regUri(&uri_stream_stop, "/api/camera/stream-stop");
    regUri(&uri_stream_frame, "/api/camera/stream-frame");

    ESP_LOGI(TAG, "Web server started successfully on port 80!");
    ESP_LOGI(TAG, "All URI handlers registered");
}

void WebServerService::stop() {
    if (_server) {
        if (_streamingActive) {
            UnitCamS3_5MP::getInstance().StopStreamingMode();
            _streamingActive = false;
        }
        httpd_stop(_server);
        _server = nullptr;
        ESP_LOGI(TAG, "Web server stopped");
    }
}