#include "camera/UnitCamS3_5MP.h"
#include "services/StorageService.h"
#include "services/WifiService.h"
#include "services/WebServerService.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "UnitCamS3_5MP";
static const char* HTTP_TAG = "CameraUpload";
const char* UnitCamS3_5MP::_httpBoundary = "-------562164BDF";

esp_err_t UnitCamS3_5MP::httpEventHandler(esp_http_client_event_t* evt) {
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(HTTP_TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(HTTP_TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(HTTP_TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            break;
        case HTTP_EVENT_ON_DATA:
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(HTTP_TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(HTTP_TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        default:
            break;
    }
    return ESP_OK;
}

void UnitCamS3_5MP::uploadPhoto(camera_fb_t* fb) {
    if (!fb || !fb->buf || fb->len == 0) {
        ESP_LOGE(HTTP_TAG, "Invalid frame buffer for upload");
        return;
    }
    
    StorageService& storage = StorageService::getInstance();
    const char* serverUrl = storage.getConfig().postServer.c_str();
    bool usePut = storage.getConfig().postUsePut;

    if (strlen(serverUrl) == 0) {
        ESP_LOGI(HTTP_TAG, "No upload server configured, skipping upload");
        return;
    }

    esp_http_client_config_t config = {};
    config.url = serverUrl;
    config.event_handler = httpEventHandler;
    config.timeout_ms = 15000;
    config.transport_type = HTTP_TRANSPORT_OVER_SSL;
    config.skip_cert_common_name_check = true;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(HTTP_TAG, "Failed to init HTTP client");
        return;
    }

    int contentLength;
    
    if (usePut) {
        contentLength = fb->len;
        esp_http_client_set_method(client, HTTP_METHOD_PUT);
        esp_http_client_set_header(client, "Content-Type", "image/jpeg");
    } else {
        contentLength = strlen(_postHeader) + fb->len + strlen(_postFooter);
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        esp_http_client_set_header(client, "Accept", "*/*");
        esp_http_client_set_header(client, "Connection", "close");
        
        char content_type[64];
        snprintf(content_type, sizeof(content_type), "multipart/form-data; boundary=%s", _httpBoundary);
        esp_http_client_set_header(client, "Content-Type", content_type);
    }
    
    char content_length_hdr[32];
    snprintf(content_length_hdr, sizeof(content_length_hdr), "%d", contentLength);
    esp_http_client_set_header(client, "Content-Length", content_length_hdr);

    esp_err_t err = esp_http_client_open(client, contentLength);
    if (err != ESP_OK) {
        ESP_LOGE(HTTP_TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return;
    }

    int written = 0;
    
    const int WRITE_CHUNK = 65536;  // 64KB
    size_t writeSize = usePut ? fb->len : strlen(_postHeader);
    const char* writePtr = usePut ? (const char*)fb->buf : _postHeader;
    
    for (size_t offset = 0; offset < writeSize; offset += WRITE_CHUNK) {
        size_t chunk = writeSize - offset;
        if (chunk > WRITE_CHUNK) chunk = WRITE_CHUNK;
        int w = esp_http_client_write(client, writePtr + offset, chunk);
        if (w < 0) {
            ESP_LOGE(HTTP_TAG, "Write failed at offset %zu", offset);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return;
        }
        written += w;
    }
    
    if (!usePut) {
        for (size_t offset = 0; offset < fb->len; offset += WRITE_CHUNK) {
            size_t chunk = fb->len - offset;
            if (chunk > WRITE_CHUNK) chunk = WRITE_CHUNK;
            int w = esp_http_client_write(client, (const char*)fb->buf + offset, chunk);
            if (w < 0) {
                ESP_LOGE(HTTP_TAG, "Write image failed at offset %zu", offset);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return;
            }
            written += w;
        }
        written += esp_http_client_write(client, _postFooter, strlen(_postFooter));
    }
    
    ESP_LOGI(HTTP_TAG, "Total bytes written: %d / %d (method: %s)", 
             written, contentLength, usePut ? "PUT" : "POST");

    int fetch_ret = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(HTTP_TAG, "fetch_headers ret=%d, status=%d", fetch_ret, status_code);
    
    if (fetch_ret < 0) {
        ESP_LOGE(HTTP_TAG, "Failed to fetch response headers");
    } else if (status_code >= 200 && status_code < 300) {
        ESP_LOGI(HTTP_TAG, "Upload successful");
    } else {
        ESP_LOGW(HTTP_TAG, "Upload failed, status code: %d", status_code);
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}

void UnitCamS3_5MP::SetLed(bool state) {
    gpio_set_level((gpio_num_t)HAL_PIN_LED, state ? 0 : 1);
}

void UnitCamS3_5MP::lockCamera() {
    if (!_lockInitialized && _cameraMutex == nullptr) {
        _cameraMutex = xSemaphoreCreateRecursiveMutex();
        _lockInitialized = true;
    }
    if (_cameraMutex) {
        xSemaphoreTakeRecursive(_cameraMutex, portMAX_DELAY);
    }
}

void UnitCamS3_5MP::unlockCamera() {
    if (_cameraMutex) {
        xSemaphoreGiveRecursive(_cameraMutex);
    }
}

void UnitCamS3_5MP::TakePhoto(std::function<void(camera_fb_t*)> processPhoto) {
    // RAII锁守卫：任何return路径都会自动解锁
    CameraLockGuard lock(*this);
    
    if (!_initialized) {
        ESP_LOGI(TAG, "Camera not initialized, initializing...");
        cam_init();
    }

    if (!_initialized) {
        ESP_LOGE(TAG, "TakePhoto aborted: camera not initialized");
        return;
    }

    camera_fb_t* fb = nullptr;

    _takePhotoActive = true;

    if (OnTakePhotoStart != nullptr) {
        OnTakePhotoStart();
    }

    SetLed(true);

    ESP_LOGI(TAG, "Discarding first frame (may be stale)...");
    camera_fb_t* stale_fb = esp_camera_fb_get();
    if (stale_fb) {
        esp_camera_fb_return(stale_fb);
        stale_fb = nullptr;
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);

    for (int i = 0; i < 3; i++) {
        fb = esp_camera_fb_get();
        if (fb) {
            if (i > 0) {
                ESP_LOGI(TAG, "Got valid frame on attempt %d", i + 1);
            }
            break;
        }
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
    
    if (!fb) {
        ESP_LOGI(TAG, "Failed to get camera frame");
        SetLed(false);
        _takePhotoActive = false;
        if (OnTakePhotoEnd != nullptr) {
            OnTakePhotoEnd();
        }
        return;
    }

    ESP_LOGI(TAG, "captured height: %d , width: %d, length: %d", fb->height, fb->width, fb->len);

    if (processPhoto) {
        processPhoto(fb);
    }

    esp_camera_fb_return(fb);
    fb = nullptr;

    _takePhotoActive = false;

    if (OnTakePhotoEnd != nullptr) {
        OnTakePhotoEnd();
    }

    SetLed(false);
}

void UnitCamS3_5MP::cam_init() {
    // 调用cam_init时外层必须已经持有锁
    esp_camera_deinit();

    StorageService& storage = StorageService::getInstance();
    const CONFIG::SystemConfig_t& config = storage.getConfig();

    camera_config_t cameraConfig = {};
    cameraConfig.ledc_channel = LEDC_CHANNEL_0;
    cameraConfig.ledc_timer = LEDC_TIMER_0;
    cameraConfig.pin_d0 = CAMERA_PIN_D0;
    cameraConfig.pin_d1 = CAMERA_PIN_D1;
    cameraConfig.pin_d2 = CAMERA_PIN_D2;
    cameraConfig.pin_d3 = CAMERA_PIN_D3;
    cameraConfig.pin_d4 = CAMERA_PIN_D4;
    cameraConfig.pin_d5 = CAMERA_PIN_D5;
    cameraConfig.pin_d6 = CAMERA_PIN_D6;
    cameraConfig.pin_d7 = CAMERA_PIN_D7;
    cameraConfig.pin_xclk = CAMERA_PIN_XCLK;
    cameraConfig.pin_pclk = CAMERA_PIN_PCLK;
    cameraConfig.pin_vsync = CAMERA_PIN_VSYNC;
    cameraConfig.pin_href = CAMERA_PIN_HREF;
    cameraConfig.pin_sccb_sda = CAMERA_PIN_SIOD;
    cameraConfig.pin_sccb_scl = CAMERA_PIN_SIOC;
    cameraConfig.pin_pwdn = CAMERA_PIN_PWDN;
    cameraConfig.pin_reset = CAMERA_PIN_RESET;
    // XCLK 动态调整：推流模式(VGA)用 24MHz 最大化帧率；拍照模式(可能 5MP)用 20MHz 保证 DMA 不溢出
    cameraConfig.xclk_freq_hz = _streamingMode ? XCLK_FREQ_HZ : 20000000;
    cameraConfig.pixel_format = PIXFORMAT_JPEG;
    // 推流模式使用独立的 streamFrameSize（最高 VGA），拍照模式使用 frameSize（最高 5MP）
    int activeFrameSize = _streamingMode ? config.streamFrameSize : config.frameSize;
    if (_streamingMode && (activeFrameSize < 0 || activeFrameSize > (int)FRAMESIZE_VGA)) {
        activeFrameSize = (int)FRAMESIZE_VGA;
    }
    cameraConfig.frame_size = (framesize_t)activeFrameSize;
    // 拍照高质量；推流模式降低质量加快编码并减小数据量
    cameraConfig.jpeg_quality = _streamingMode ? 12 : 8;

    if (_streamingMode) {
        cameraConfig.fb_count = 2;
        cameraConfig.grab_mode = CAMERA_GRAB_LATEST;
    } else {
        cameraConfig.fb_count = 1;
        cameraConfig.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    }
    cameraConfig.fb_location = CAMERA_FB_IN_PSRAM;
    cameraConfig.sccb_i2c_port = 1;

    // 5MP 帧缓冲较大，PSRAM 碎片化时可能首次分配失败，重试一次
    esp_err_t err = ESP_FAIL;
    for (int attempt = 1; attempt <= 2; attempt++) {
        err = esp_camera_init(&cameraConfig);
        if (err == ESP_OK) {
            break;
        }
        ESP_LOGE(TAG, "camera init attempt %d failed: %s", attempt, esp_err_to_name(err));
        if (attempt < 2) {
            esp_camera_deinit();
            vTaskDelay(200 / portTICK_PERIOD_MS);
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "camera init failed after retries: %s", esp_err_to_name(err));
        _initialized = false;
        return;
    }
    _initialized = true;
    
    // 调整相机参数
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        ESP_LOGI(TAG, "Setting camera parameters...");

        s->set_framesize(s, (framesize_t)activeFrameSize);

        int jpegQuality = (config.jpegQuantity <= 2) ? config.jpegQuantity : 1;
        s->set_quality(s, jpegQuality);
        s->set_special_effect(s, config.specialEffect);
        s->set_wb_mode(s, config.wbMode);
        s->set_saturation(s, config.saturation);
        s->set_contrast(s, config.contrast);
        s->set_brightness(s, config.brightness);

        ESP_LOGI(TAG, "Camera parameters set");

        if (_streamingMode) {
            if (s->set_gain_ctrl) s->set_gain_ctrl(s, 0);
            if (s->set_exposure_ctrl) s->set_exposure_ctrl(s, 0);
            if (s->set_whitebal) s->set_whitebal(s, 0);
            if (s->set_aec2) s->set_aec2(s, 0);
            if (s->set_denoise) s->set_denoise(s, 0);
            if (s->set_gainceiling) s->set_gainceiling(s, GAINCEILING_8X);
            ESP_LOGI(TAG, "Streaming mode: disabled AGC/AEC/AWB for max framerate");
        }
    }
    
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "camera ready after stabilization");
}

void UnitCamS3_5MP::led_init() {
    gpio_reset_pin((gpio_num_t)HAL_PIN_LED);
    gpio_set_direction((gpio_num_t)HAL_PIN_LED, GPIO_MODE_OUTPUT);
    gpio_set_pull_mode((gpio_num_t)HAL_PIN_LED, GPIO_PULLUP_ONLY);
}

void UnitCamS3_5MP::ReloadConfig() {
    // RAII锁守卫：保证任何路径都能解锁
    CameraLockGuard lock(*this);
    
    ESP_LOGI(TAG, "Reloading camera config...");

    // 通知上层释放占用的缓冲区
    if (onBeforeReload) {
        onBeforeReload();
    }

    if (_initialized) {
        esp_camera_deinit();
        _initialized = false;
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
    cam_init();
}

void UnitCamS3_5MP::SetFrameSize(framesize_t frameSize) {
    StorageService& storage = StorageService::getInstance();
    CONFIG::SystemConfig_t config = storage.getConfig();
    config.frameSize = (int)frameSize;
    storage.setConfig(config);
    ESP_LOGI(TAG, "Set frame size to %d, reloading camera...", (int)frameSize);
    ReloadConfig();
}

void UnitCamS3_5MP::SetJpegQuality(int quality) {
    CameraLockGuard lock(*this);
    
    StorageService& storage = StorageService::getInstance();
    CONFIG::SystemConfig_t config = storage.getConfig();
    config.jpegQuantity = quality;
    storage.setConfig(config);

    int jpegQuality = (quality <= 2) ? quality : 1;
    if (_initialized) {
        sensor_t* s = esp_camera_sensor_get();
        if (s) {
            s->set_quality(s, jpegQuality);
            ESP_LOGI(TAG, "Set JPEG quality to %d", jpegQuality);
        }
    } else {
        ESP_LOGI(TAG, "Camera not initialized, quality will apply on next init");
    }
}

void UnitCamS3_5MP::SetWhiteBalance(int wbMode) {
    CameraLockGuard lock(*this);
    
    StorageService& storage = StorageService::getInstance();
    CONFIG::SystemConfig_t config = storage.getConfig();
    config.wbMode = wbMode;
    storage.setConfig(config);

    if (_initialized) {
        sensor_t* s = esp_camera_sensor_get();
        if (s) {
            s->set_wb_mode(s, wbMode);
            ESP_LOGI(TAG, "Set white balance to %d", wbMode);
        }
    }
}

void UnitCamS3_5MP::SetContrast(int contrast) {
    CameraLockGuard lock(*this);
    
    StorageService& storage = StorageService::getInstance();
    CONFIG::SystemConfig_t config = storage.getConfig();
    config.contrast = contrast;
    storage.setConfig(config);

    if (_initialized) {
        sensor_t* s = esp_camera_sensor_get();
        if (s) {
            s->set_contrast(s, contrast);
            ESP_LOGI(TAG, "Set contrast to %d", contrast);
        }
    }
}

void UnitCamS3_5MP::SetSaturation(int saturation) {
    CameraLockGuard lock(*this);
    
    StorageService& storage = StorageService::getInstance();
    CONFIG::SystemConfig_t config = storage.getConfig();
    config.saturation = saturation;
    storage.setConfig(config);

    if (_initialized) {
        sensor_t* s = esp_camera_sensor_get();
        if (s) {
            s->set_saturation(s, saturation);
            ESP_LOGI(TAG, "Set saturation to %d", saturation);
        }
    }
}

void UnitCamS3_5MP::SetBrightness(int brightness) {
    CameraLockGuard lock(*this);
    
    StorageService& storage = StorageService::getInstance();
    CONFIG::SystemConfig_t config = storage.getConfig();
    config.brightness = brightness;
    storage.setConfig(config);

    if (_initialized) {
        sensor_t* s = esp_camera_sensor_get();
        if (s) {
            s->set_brightness(s, brightness);
            ESP_LOGI(TAG, "Set brightness to %d", brightness);
        }
    }
}

void UnitCamS3_5MP::SetSpecialEffect(int effect) {
    CameraLockGuard lock(*this);
    
    StorageService& storage = StorageService::getInstance();
    CONFIG::SystemConfig_t config = storage.getConfig();
    config.specialEffect = effect;
    storage.setConfig(config);

    if (_initialized) {
        sensor_t* s = esp_camera_sensor_get();
        if (s) {
            s->set_special_effect(s, effect);
            ESP_LOGI(TAG, "Set special effect to %d", effect);
        }
    }
}

void UnitCamS3_5MP::SetAllCameraConfig(int frameSize, int jpegQuality, int wbMode, int specialEffect, int contrast, int saturation, int brightness) {
    // RAII锁：递归锁支持嵌套调用，不会死锁
    CameraLockGuard lock(*this);
    
    ESP_LOGI(TAG, "Setting all camera config at once");
    
    StorageService& storage = StorageService::getInstance();
    CONFIG::SystemConfig_t config = storage.getConfig();
    config.frameSize = frameSize;
    config.jpegQuantity = jpegQuality;
    config.wbMode = wbMode;
    config.specialEffect = specialEffect;
    config.contrast = contrast;
    config.saturation = saturation;
    config.brightness = brightness;
    storage.setConfig(config);
    
    // 通知上层释放缓冲区
    if (onBeforeReload) {
        onBeforeReload();
    }

    if (_initialized) {
        esp_camera_deinit();
        _initialized = false;
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
    cam_init();
}

void UnitCamS3_5MP::ApplyCameraConfig() {
    ReloadConfig();
}

void UnitCamS3_5MP::StartStreamingMode() {
    ESP_LOGI(TAG, "Entering streaming mode");
    if (_streamingMode) {
        ESP_LOGI(TAG, "Already in streaming mode");
        return;
    }

    StorageService& storage = StorageService::getInstance();
    CONFIG::SystemConfig_t config = storage.getConfig();
    if (config.streamFrameSize < 0 || config.streamFrameSize > (int)FRAMESIZE_VGA) {
        config.streamFrameSize = (int)FRAMESIZE_VGA;
        storage.setConfig(config);
    }

    _streamingMode = true;
    ReloadConfig();
}

void UnitCamS3_5MP::StopStreamingMode() {
    ESP_LOGI(TAG, "Leaving streaming mode");
    if (!_streamingMode) {
        ESP_LOGI(TAG, "Not in streaming mode, nothing to do");
        return;
    }

    _streamingMode = false;
    ReloadConfig();
}

void UnitCamS3_5MP::Init() {
    StorageService& storage = StorageService::getInstance();
    
    bool hasWifiConfig = false;
    for (int i = 0; i < MAX_WIFI_SSIDS; i++) {
        if (!storage.getConfig().wifiSsid[i].empty()) {
            hasWifiConfig = true;
            break;
        }
    }
    
    if (!hasWifiConfig) {
        CONFIG::SystemConfig_t defaultConfig;
        defaultConfig.wifiSsid[0] = "s20154530";
        defaultConfig.wifiPass[0] = "Y20154530";
        storage.setConfig(defaultConfig);
    }

    // 初始化multipart/form-data POST数据
    snprintf(_postHeader, sizeof(_postHeader),
             "--%s\r\n"
             "Content-Disposition: form-data; name=\"image\"; filename=\"photo.jpeg\"\r\n"
             "Content-Type: image/jpeg\r\n\r\n", _httpBoundary);
    snprintf(_postFooter, sizeof(_postFooter), "\r\n--%s--\r\n", _httpBoundary);

    led_init();
}

void UnitCamS3_5MP::Start() {
    StorageService& storage = StorageService::getInstance();
    int bootMode = storage.getConfig().bootMode;
    
    ESP_LOGI(TAG, "Boot mode: %s", bootMode == CONFIG::BOOT_MODE_MONITOR ? "Monitor" : "Interactive");
    
    if (bootMode == CONFIG::BOOT_MODE_MONITOR) {
        if (storage.getConfig().postServer.empty()) {
            ESP_LOGW(TAG, "Monitor mode selected but no server configured, falling back to interactive mode");
            StartForSetting();
            return;
        }
        StartForMonitor();
    } else {
        StartForSetting();
    }
}

void UnitCamS3_5MP::StartForSetting() {
    WebServerService& webServer = WebServerService::getInstance();
    webServer.onTakePhotoRequested = []() {
        UnitCamS3_5MP& camera = UnitCamS3_5MP::getInstance();
        camera.WebTakePhoto([](camera_fb_t* fb) {
            WebServerService::notifyPhotoCaptured(fb);
        });
    };

    onBeforeReload = []() {
        WebServerService::releasePhotoBuffer();
    };

    WifiService& wifi = WifiService::getInstance();
    wifi.init();
    wifi.connect(true);

    while (true) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

// 监控模式任务
void UnitCamS3_5MP::monitorTask(void* arg) {
    UnitCamS3_5MP* camera = static_cast<UnitCamS3_5MP*>(arg);
    StorageService& storage = StorageService::getInstance();
    
    ESP_LOGI(TAG, "Monitor task started");
    
    // 相机初始化（使用RAII锁，任何错误都会自动解锁）
    bool initOk = false;
    {
        CameraLockGuard lock(*camera);
        camera->cam_init();
        initOk = camera->_initialized;
    }
    
    if (!initOk) {
        ESP_LOGE(TAG, "Camera init failed in monitor mode, falling back to AP");
        WifiService::getInstance().startAPMode();
        camera->_monitorModeRunning = false;
        vTaskDelete(nullptr);
        return;
    }
    
    while (camera->_monitorModeRunning) {
        int interval = storage.getConfig().uploadInterval;
        if (interval < 10) interval = 60;
        
        ESP_LOGI(TAG, "Taking photo for upload...");
        
        // 拍照并处理（RAII锁，任何错误路径自动解锁）
        camera_fb_t* fb = nullptr;
        bool captureOk = false;
        
        {
            CameraLockGuard lock(*camera);
            
            if (camera->_initialized) {
                camera_fb_t* stale_fb = esp_camera_fb_get();
                if (stale_fb) {
                    esp_camera_fb_return(stale_fb);
                }
                vTaskDelay(pdMS_TO_TICKS(100));
                
                fb = esp_camera_fb_get();
                if (fb) {
                    captureOk = true;
                    ESP_LOGI(TAG, "Photo captured: %zu bytes", fb->len);
                    
                    // 直接调用内部上传方法，不需要外部回调
                    camera->uploadPhoto(fb);
                    
                    esp_camera_fb_return(fb);
                    fb = nullptr;
                } else {
                    ESP_LOGE(TAG, "Failed to capture photo: fb is null");
                    camera->_initialized = false;
                }
            } else {
                ESP_LOGW(TAG, "Camera not initialized, reinitializing...");
                camera->cam_init();
            }
        }
        // 锁在这里自动释放
        
        if (!captureOk && !camera->_initialized) {
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
        
        // 等待上传间隔，每秒检查一次是否需要退出
        int waitMs = interval * 1000;
        int waitedMs = 0;
        while (waitedMs < waitMs && camera->_monitorModeRunning) {
            vTaskDelay(pdMS_TO_TICKS(100));
            waitedMs += 100;
        }
    }
    
    ESP_LOGI(TAG, "Monitor task exiting");
    camera->_monitorModeRunning = false;
    vTaskDelete(nullptr);
}

void UnitCamS3_5MP::StartForMonitor() {
    _monitorModeRunning = true;
    
    WebServerService& webServer = WebServerService::getInstance();
    webServer.onTakePhotoRequested = []() {
        UnitCamS3_5MP& camera = UnitCamS3_5MP::getInstance();
        camera.WebTakePhoto([](camera_fb_t* fb) {
            WebServerService::notifyPhotoCaptured(fb);
        });
    };

    onBeforeReload = []() {
        WebServerService::releasePhotoBuffer();
    };
    
    WifiService& wifi = WifiService::getInstance();
    wifi.init();
    wifi.connect(false);
    
    if (wifi.isConnected()) {
        ESP_LOGI(TAG, "WiFi connected, starting monitor upload task");
        xTaskCreate(monitorTask, "monitor_task", 8192, this, 5, nullptr);
    } else {
        ESP_LOGW(TAG, "WiFi connection failed, staying in AP mode (interactive)");
        _monitorModeRunning = false;
    }
    
    while (true) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

void UnitCamS3_5MP::WebTakePhoto(std::function<void(camera_fb_t* buffer)> processPhoto) {
    ESP_LOGI(TAG, "Taking photo via web...");
    TakePhoto(processPhoto);
}