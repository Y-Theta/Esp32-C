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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

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

// ===== 推流后台采集：独立 FreeRTOS 任务持续抓帧到环形队列 =====
// 队列大小固定 8，满时丢最旧帧，保证 HTTP 响应始终拿到最新帧；
// 后台任务仅做 esp_camera_fb_get -> 拷贝到 PSRAM -> esp_camera_fb_return，极轻量
static const int STREAM_RING_SIZE = 8;
struct StreamFrame {
    uint8_t* jpeg;
    size_t   len;
    uint64_t ts;
};
static StreamFrame  s_streamRing[STREAM_RING_SIZE] = {};
static int          s_streamRingHead = 0;      // 下一写入位置
static int          s_streamRingCount = 0;     // 当前有效帧数
static TaskHandle_t s_streamCaptureTask = nullptr;
static volatile bool s_streamCaptureRun = false;
static portMUX_TYPE s_streamRingMux = portMUX_INITIALIZER_UNLOCKED;

// 前向声明
static void startStreamCaptureTask();
static void stopStreamCaptureTask();

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

// WebSocket 推流端点：method 必须为 HTTP_GET，is_websocket = true
static const httpd_uri_t uri_stream_ws = {
    .uri = "/ws/camera/stream",
    .method = HTTP_GET,
    .handler = WebServerService::apiStreamWsHandler,
    .user_ctx = nullptr,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = nullptr
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
    
    // 返回WiFi列表
    cJSON* wifiList = cJSON_CreateArray();
    for (int i = 0; i < MAX_WIFI_SSIDS; i++) {
        cJSON* wifiItem = cJSON_CreateObject();
        cJSON_AddStringToObject(wifiItem, "ssid", config.wifiSsid[i].c_str());
        cJSON_AddStringToObject(wifiItem, "pass", config.wifiPass[i].c_str());
        cJSON_AddItemToArray(wifiList, wifiItem);
    }
    cJSON_AddItemToObject(root, "wifiList", wifiList);
    
    // 兼容旧版本，返回第一个WiFi
    cJSON_AddStringToObject(root, "wifiSsid", config.wifiSsid[0].c_str());
    cJSON_AddStringToObject(root, "wifiPass", config.wifiPass[0].c_str());
    
    cJSON_AddStringToObject(root, "postServer", config.postServer.c_str());
    cJSON_AddBoolToObject(root, "postUsePut", config.postUsePut);
    
    cJSON_AddStringToObject(root, "startPoster", config.startPoster.c_str());
    cJSON_AddStringToObject(root, "waitApFirst", config.waitApFirst.c_str());
    
    cJSON_AddNumberToObject(root, "jpegQuantity", config.jpegQuantity);
    cJSON_AddNumberToObject(root, "frameSize", config.frameSize);
    cJSON_AddNumberToObject(root, "streamFps", config.streamFps);
    cJSON_AddNumberToObject(root, "streamFrameSize", config.streamFrameSize);
    cJSON_AddNumberToObject(root, "streamFbCount", config.streamFbCount);
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
    
    // 处理WiFi列表
    cJSON* wifiList = cJSON_GetObjectItem(root, "wifiList");
    if (wifiList && cJSON_IsArray(wifiList)) {
        int count = cJSON_GetArraySize(wifiList);
        for (int i = 0; i < count && i < MAX_WIFI_SSIDS; i++) {
            cJSON* wifiItem = cJSON_GetArrayItem(wifiList, i);
            if (wifiItem && cJSON_IsObject(wifiItem)) {
                cJSON* ssid = cJSON_GetObjectItem(wifiItem, "ssid");
                cJSON* pass = cJSON_GetObjectItem(wifiItem, "pass");
                if (ssid && cJSON_IsString(ssid)) config.wifiSsid[i] = ssid->valuestring;
                if (pass && cJSON_IsString(pass)) config.wifiPass[i] = pass->valuestring;
            }
        }
    } else {
        // 兼容旧版本单WiFi
        item = cJSON_GetObjectItem(root, "wifiSsid");
        if (item && cJSON_IsString(item)) config.wifiSsid[0] = item->valuestring;
        
        item = cJSON_GetObjectItem(root, "wifiPass");
        if (item && cJSON_IsString(item)) config.wifiPass[0] = item->valuestring;
    }
    
    item = cJSON_GetObjectItem(root, "postServer");
    if (item && cJSON_IsString(item)) config.postServer = item->valuestring;
    
    item = cJSON_GetObjectItem(root, "postUsePut");
    if (item) {
        if (cJSON_IsBool(item)) config.postUsePut = cJSON_IsTrue(item);
        else if (cJSON_IsNumber(item)) config.postUsePut = (item->valueint != 0);
    }

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

    item = cJSON_GetObjectItem(root, "streamFbCount");
    if (item && cJSON_IsNumber(item)) {
        int n = item->valueint;
        if (n >= 1 && n <= 4) config.streamFbCount = n;
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
    cJSON* streamFbCountJson = cJSON_GetObjectItem(root, "streamFbCount");

    int frameSize = 10;
    int jpegQuality = 1;
    int wbMode = 0;
    int specialEffect = 0;
    int contrast = 3;
    int saturation = 3;
    int brightness = 4;
    int streamFps = 20;
    int streamFrameSize = (int)FRAMESIZE_VGA;
    int streamFbCount = 2;

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
    if (streamFbCountJson && cJSON_IsNumber(streamFbCountJson)) {
        streamFbCount = streamFbCountJson->valueint;
        if (streamFbCount < 1) streamFbCount = 1;
        if (streamFbCount > 4) streamFbCount = 4;
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
    config.streamFbCount = streamFbCount;
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
    cJSON_AddNumberToObject(root, "streamFbCount", config.streamFbCount);
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
    startStreamCaptureTask();  // 相机模式切换完成后启动后台采集线程

    StorageService& storage = StorageService::getInstance();
    const CONFIG::SystemConfig_t& config = storage.getConfig();
    
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddNumberToObject(root, "streamFrameSize", config.streamFrameSize);
    cJSON_AddNumberToObject(root, "streamFbCount", config.streamFbCount);
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
    _streamingActive = false;              // 先置 false，WS 发送任务会自行检测退出
    stopStreamCaptureTask();               // 停止后台采集，释放环形队列
    vTaskDelay(pdMS_TO_TICKS(50));         // 等待 WS 发送任务感知 alive=false 并退出
    camera.StopStreamingMode();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

// ===== WebSocket 推流：后台线程持续抓帧缓存到环形队列，WS 发送任务逐帧推送 =====
// WS 二进制帧直接承载 JPEG 原始数据，不加额外帧头（前端收到即可渲染），最小开销
// 每帧发送前若队列积压超过阈值，会丢最旧帧追到最新，保证实时性（低延迟）
static const int STREAM_RING_KEEP = 2;  // 允许队列中保留的最大积压帧数（超过则丢旧追新）

// 环形队列：入队一帧；队列满则丢最旧帧
static void streamRingPush(uint8_t* jpeg, size_t len, uint64_t ts) {
    if (!jpeg || len == 0) { free(jpeg); return; }
    portENTER_CRITICAL(&s_streamRingMux);
    if (s_streamRingCount == STREAM_RING_SIZE) {
        free(s_streamRing[s_streamRingHead].jpeg);
        s_streamRing[s_streamRingHead].jpeg = nullptr;
        s_streamRing[s_streamRingHead].len = 0;
        s_streamRing[s_streamRingHead].ts = 0;
        s_streamRingHead = (s_streamRingHead + 1) % STREAM_RING_SIZE;
        s_streamRingCount--;
    }
    int writeIdx = (s_streamRingHead + s_streamRingCount) % STREAM_RING_SIZE;
    s_streamRing[writeIdx].jpeg = jpeg;
    s_streamRing[writeIdx].len = len;
    s_streamRing[writeIdx].ts = ts;
    s_streamRingCount++;
    portEXIT_CRITICAL(&s_streamRingMux);
}

// 环形队列：弹出最旧一帧；成功返回 true 并把所有权交给调用方（需 free(jpeg)）
static bool streamRingPop(StreamFrame* out) {
    bool ok = false;
    portENTER_CRITICAL(&s_streamRingMux);
    if (s_streamRingCount > 0) {
        *out = s_streamRing[s_streamRingHead];
        s_streamRing[s_streamRingHead].jpeg = nullptr;
        s_streamRing[s_streamRingHead].len = 0;
        s_streamRing[s_streamRingHead].ts = 0;
        s_streamRingHead = (s_streamRingHead + 1) % STREAM_RING_SIZE;
        s_streamRingCount--;
        ok = true;
    }
    portEXIT_CRITICAL(&s_streamRingMux);
    return ok;
}

// 环形队列：丢帧追到最新，保留 keep 帧。返回被丢弃帧数
static int streamRingCatchUp(int keep) {
    int dropped = 0;
    portENTER_CRITICAL(&s_streamRingMux);
    while (s_streamRingCount > keep) {
        free(s_streamRing[s_streamRingHead].jpeg);
        s_streamRing[s_streamRingHead].jpeg = nullptr;
        s_streamRing[s_streamRingHead].len = 0;
        s_streamRing[s_streamRingHead].ts = 0;
        s_streamRingHead = (s_streamRingHead + 1) % STREAM_RING_SIZE;
        s_streamRingCount--;
        dropped++;
    }
    portEXIT_CRITICAL(&s_streamRingMux);
    return dropped;
}

static void streamRingClear() {
    portENTER_CRITICAL(&s_streamRingMux);
    for (int i = 0; i < STREAM_RING_SIZE; i++) {
        if (s_streamRing[i].jpeg) {
            free(s_streamRing[i].jpeg);
            s_streamRing[i].jpeg = nullptr;
            s_streamRing[i].len = 0;
            s_streamRing[i].ts = 0;
        }
    }
    s_streamRingHead = 0;
    s_streamRingCount = 0;
    portEXIT_CRITICAL(&s_streamRingMux);
}

// 后台采集任务：持续抓帧，拷贝入环形队列（不阻塞相机）
static void streamCaptureTask(void*) {
    ESP_LOGI(TAG, "Stream capture task started");
    while (s_streamCaptureRun) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        uint64_t ts = getCurrentTimeMs();
        uint8_t* copy = nullptr;
        size_t len = fb->len;
        if (fb->format == PIXFORMAT_JPEG && len > 0) {
            copy = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
            if (copy) {
                memcpy(copy, fb->buf, len);
            }
        }
        esp_camera_fb_return(fb);
        if (copy) {
            streamRingPush(copy, len, ts);
        }
        taskYIELD();
    }
    streamRingClear();
    s_streamCaptureTask = nullptr;
    vTaskDelete(nullptr);
}

static void startStreamCaptureTask() {
    if (s_streamCaptureTask) return;
    s_streamCaptureRun = true;
    streamRingClear();
    xTaskCreate(streamCaptureTask, "stream_cap", 4096,
                nullptr, tskIDLE_PRIORITY + 4, &s_streamCaptureTask);
}

static void stopStreamCaptureTask() {
    s_streamCaptureRun = false;
    if (s_streamCaptureTask) {
        int waits = 0;
        while (s_streamCaptureTask && waits < 50) {
            vTaskDelay(pdMS_TO_TICKS(10));
            waits++;
        }
        if (s_streamCaptureTask) {
            vTaskDelete(s_streamCaptureTask);
            s_streamCaptureTask = nullptr;
        }
    }
    streamRingClear();
}

// ===== 每个 WebSocket 连接一个独立发送任务 =====
struct WsSessCtx {
    httpd_handle_t hd;
    int fd;
    TaskHandle_t sendTask;
    volatile bool alive;
};
static const int    WS_SEND_STACK = 4096;
static const UBaseType_t WS_SEND_PRIO = tskIDLE_PRIORITY + 5;

static void streamWsSendTask(void* arg) {
    WsSessCtx* ctx = (WsSessCtx*)arg;
    ESP_LOGI(TAG, "WS stream sender started, fd=%d", ctx->fd);
    while (ctx->alive && _streamingActive) {
        // 追帧：如果队列积压过多，丢最旧追到最新，保持低延迟
        streamRingCatchUp(STREAM_RING_KEEP);

        StreamFrame frame;
        if (!streamRingPop(&frame)) {
            // 队列为空，短暂等待（不丢 CPU）
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }

        // 通过 WS 发送二进制帧（直接 JPEG 原始数据，无帧头）
        httpd_ws_frame_t pkt = {};
        pkt.final = true;
        pkt.fragmented = false;
        pkt.type = HTTPD_WS_TYPE_BINARY;
        pkt.payload = frame.jpeg;
        pkt.len = frame.len;
        esp_err_t err = httpd_ws_send_frame_async(ctx->hd, ctx->fd, &pkt);
        free(frame.jpeg);

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "WS send failed fd=%d err=%d, closing", ctx->fd, (int)err);
            ctx->alive = false;
            break;
        }
    }
    ESP_LOGI(TAG, "WS stream sender exited, fd=%d", ctx->fd);
    ctx->sendTask = nullptr;
    free(ctx);
    vTaskDelete(nullptr);
}

// WebSocket 推流端点 handler：处理 WS 事件（握手/收包/关闭）
// 前端应在 onopen 后发送一条文本消息（如 "start"）触发发送任务创建
esp_err_t WebServerService::apiStreamWsHandler(httpd_req_t* req) {
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WS stream handshake, fd=%d", httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(ws_pkt));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t err = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WS recv info failed err=%d fd=%d", (int)err, httpd_req_to_sockfd(req));
        return err;
    }

    int fd = httpd_req_to_sockfd(req);

    uint8_t* payloadBuf = nullptr;
    if (ws_pkt.len > 0) {
        payloadBuf = (uint8_t*)calloc(1, ws_pkt.len + 1);
        if (payloadBuf) {
            ws_pkt.payload = payloadBuf;
            err = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "WS recv payload failed err=%d fd=%d", (int)err, fd);
                free(payloadBuf);
                payloadBuf = nullptr;
                ws_pkt.payload = nullptr;
            }
        }
    }

    switch (ws_pkt.type) {
    case HTTPD_WS_TYPE_TEXT: {
        // 客户端发送 "start"：为该连接创建独立发送任务
        // 前端保证 open 后只发一次 "start"，不会重复创建
        if (_streamingActive) {
            auto* ctx = (WsSessCtx*)calloc(1, sizeof(WsSessCtx));
            if (ctx) {
                ctx->hd = req->handle;
                ctx->fd = fd;
                ctx->alive = true;
                ctx->sendTask = nullptr;
                ESP_LOGI(TAG, "WS creating sender task, fd=%d, msg=%s", fd,
                         payloadBuf ? (char*)payloadBuf : "");
                xTaskCreate(streamWsSendTask, "ws_send", WS_SEND_STACK,
                            ctx, WS_SEND_PRIO, &ctx->sendTask);
            }
        }
        break;
    }

    case HTTPD_WS_TYPE_CLOSE: {
        ESP_LOGI(TAG, "WS close fd=%d", fd);
        // 无需手动清理：发送任务下次 httpd_ws_send_data_async 会返回错误，自行退出并 free(ctx)
        httpd_ws_frame_t close_pkt = {};
        close_pkt.final = true;
        close_pkt.type = HTTPD_WS_TYPE_CLOSE;
        httpd_ws_send_frame(req, &close_pkt);
        break;
    }

    default:
        break;
    }

    free(payloadBuf);
    return ESP_OK;
}

// 拍照完成回调，由相机调用
void WebServerService::notifyPhotoCaptured(camera_fb_t* fb) {
    if (!fb) {
        _isTakingPhoto = false;
        return;
    }

    if (_photoBuffer) {
        free(_photoBuffer);
        _photoBuffer = nullptr;
        _photoBufferSize = 0;
    }

    _photoBuffer = (uint8_t*)malloc(fb->len);
    if (_photoBuffer) {
        memcpy(_photoBuffer, fb->buf, fb->len);
        _photoBufferSize = fb->len;
        _photoBufferLastUseMs = getCurrentTimeMs();
        _newPhotoReady = true;
        ESP_LOGI(TAG, "New photo buffered, version: %u, size: %zu bytes", _photoVersion, fb->len);
    } else {
        ESP_LOGE(TAG, "Failed to allocate memory for photo buffer (%zu bytes)", fb->len);
        _newPhotoReady = false;
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
    regUri(&uri_stream_ws, "/ws/camera/stream");

    ESP_LOGI(TAG, "Web server started successfully on port 80!");
    ESP_LOGI(TAG, "All URI handlers registered");
}

void WebServerService::stop() {
    if (_server) {
        if (_streamingActive) {
            _streamingActive = false;
            stopStreamCaptureTask();
            vTaskDelay(pdMS_TO_TICKS(50));
            UnitCamS3_5MP::getInstance().StopStreamingMode();
        }
        httpd_stop(_server);
        _server = nullptr;
        releasePhotoBuffer();
        streamRingClear();
        _isTakingPhoto = false;
        _newPhotoReady = false;
        ESP_LOGI(TAG, "Web server stopped");
    }
}