#include "camera/UnitCamS3_5MP.h"
#include "services/StorageService.h"
#include "services/WifiService.h"
#include "services/WebServerService.h"
#include "esp_log.h"

static const char* TAG = "UnitCamS3_5MP";

void UnitCamS3_5MP::SetLed(bool state) {
    gpio_set_level((gpio_num_t)HAL_PIN_LED, state ? 0 : 1);
}

void UnitCamS3_5MP::TakePhoto(std::function<void(camera_fb_t*)> processPhoto) {
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
    } else if (OnProcessImage) {
        OnProcessImage(fb, this);
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
    // 5MP(2592x1944) 在 24MHz 下数据率超过 DMA->PSRAM 带宽会导致 EV-EOF-OVF
    cameraConfig.xclk_freq_hz = _streamingMode ? XCLK_FREQ_HZ : 20000000;
    cameraConfig.pixel_format = PIXFORMAT_JPEG;
    // 推流模式使用独立的 streamFrameSize（最高 VGA），拍照模式使用 frameSize（最高 5MP）
    // 这样切换推流不影响拍照分辨率配置，停止推流后拍照分辨率自动恢复
    int activeFrameSize = _streamingMode ? config.streamFrameSize : config.frameSize;
    if (_streamingMode && (activeFrameSize < 0 || activeFrameSize > (int)FRAMESIZE_VGA)) {
        activeFrameSize = (int)FRAMESIZE_VGA;
    }
    cameraConfig.frame_size = (framesize_t)activeFrameSize;
    // 拍照高质量；推流模式降低质量加快编码并减小数据量
    cameraConfig.jpeg_quality = _streamingMode ? 12 : 8;

    // 监控模式使用四缓冲 + LATEST 抓取最大化吞吐量
    // - 相机 DMA 一直往缓冲区写，总能找到空闲 buffer
    // - LATEST 模式总是返回最新帧，旧帧被丢弃，避免取到陈旧帧
    // - 拍照用单缓冲节省内存
    if (_streamingMode) {
        cameraConfig.fb_count = 2;
        cameraConfig.grab_mode = CAMERA_GRAB_LATEST;
    } else {
        cameraConfig.fb_count = 1;
        cameraConfig.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    }
    cameraConfig.fb_location = CAMERA_FB_IN_PSRAM;
    cameraConfig.sccb_i2c_port = 1;

    // 5MP 帧缓冲较大（4MB），PSRAM 碎片化时可能首次分配失败，重试一次
    esp_err_t err = ESP_FAIL;
    for (int attempt = 1; attempt <= 2; attempt++) {
        err = esp_camera_init(&cameraConfig);
        if (err == ESP_OK) {
            break;
        }
        ESP_LOGE(TAG, "camera init attempt %d failed: %s", attempt, esp_err_to_name(err));
        if (attempt < 2) {
            // 释放可能残留的资源并延迟后重试
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

        // 0. 设置 Frame Size - 推流模式用 streamFrameSize，拍照模式用 frameSize
        s->set_framesize(s, (framesize_t)activeFrameSize);

        // 1. 设置 JPEG 质量 (0-2 for mega_ccm: 0=高, 1=默认, 2=低)
        int jpegQuality = (config.jpegQuantity <= 2) ? config.jpegQuantity : 1;
        s->set_quality(s, jpegQuality);

        // 2. 特殊效果
        s->set_special_effect(s, config.specialEffect);

        // 3. 白平衡模式
        s->set_wb_mode(s, config.wbMode);

        // 4. 饱和度 (0-6, 3为默认)
        s->set_saturation(s, config.saturation);

        // 5. 对比度 (0-6, 3为默认)
        s->set_contrast(s, config.contrast);

        // 6. 亮度 (0-8, 4为默认)
        s->set_brightness(s, config.brightness);

        ESP_LOGI(TAG, "Camera parameters set - activeFrameSize=%d(%s mode), jpegQuality=%d, wb=%d, contrast=%d, saturation=%d, brightness=%d, effect=%d",
                 activeFrameSize, _streamingMode ? "stream" : "photo", jpegQuality, config.wbMode, config.contrast, config.saturation, config.brightness, config.specialEffect);

        // 推流模式：关闭耗时的图像处理，最大化帧率
        if (_streamingMode) {
            // 关闭自动增益控制（AGC）—— 加快曝光决策
            if (s->set_gain_ctrl) s->set_gain_ctrl(s, 0);
            // 关闭自动曝光控制（AEC）—— 加快曝光决策
            if (s->set_exposure_ctrl) s->set_exposure_ctrl(s, 0);
            // 关闭自动白平衡（AWB）—— 减少处理开销
            if (s->set_whitebal) s->set_whitebal(s, 0);
            // 关闭黑电平校准
            if (s->set_aec2) s->set_aec2(s, 0);
            // 关闭暗光模式（夜间模式会大幅降低帧率）
            if (s->set_denoise) s->set_denoise(s, 0);
            // 关闭镜头矫正
            // 关闭增益 ceilings 限制
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
    ESP_LOGI(TAG, "Reloading camera config...");

    // 等待拍照完成，避免 deinit 时 TakePhoto 还持有 fb 导致内存访问非法
    if (_takePhotoActive) {
        ESP_LOGW(TAG, "ReloadConfig: waiting for in-progress photo to finish...");
        int waitMs = 0;
        while (_takePhotoActive && waitMs < 3000) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
            waitMs += 10;
        }
        if (_takePhotoActive) {
            ESP_LOGE(TAG, "ReloadConfig: photo still in progress after 3s, aborting reload");
            return;
        }
    }

    // 通知上层释放占用的缓冲区（如 _photoBuffer），避免 PSRAM 不足导致帧缓冲分配失败
    if (onBeforeReload) {
        onBeforeReload();
    }

    if (_initialized) {
        esp_camera_deinit();
        _initialized = false;
        // 给 DMA 一点时间完全停止，避免立刻重新初始化时资源未释放
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
    ESP_LOGI(TAG, "Setting all camera config at once");
    
    // 保存所有配置到 StorageService
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
    
    // 重新初始化相机以应用所有配置
    ReloadConfig();
}

void UnitCamS3_5MP::ApplyCameraConfig() {
    ReloadConfig();
}

void UnitCamS3_5MP::StartStreamingMode() {
    ESP_LOGI(TAG, "Entering streaming mode (streamFrameSize, fb_count=4, grab_mode=LATEST)");
    if (_streamingMode) {
        ESP_LOGI(TAG, "Already in streaming mode");
        return;
    }

    // 推流使用独立的 streamFrameSize 配置（最高 VGA），不影响拍照分辨率 frameSize
    // 实际分辨率切换在 cam_init 中根据 _streamingMode 选择
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

    led_init();
}

void UnitCamS3_5MP::Start() {
    StartForSetting();
}

void UnitCamS3_5MP::StartForSetting() {
    StorageService& storage = StorageService::getInstance();

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

void UnitCamS3_5MP::WebTakePhoto(std::function<void(camera_fb_t* buffer)> processPhoto) {
    if (!_initialized) {
        ESP_LOGI(TAG, "Camera not initialized, initializing...");
        cam_init();
    }

    ESP_LOGI(TAG, "Taking photo via web...");
    TakePhoto(processPhoto);
}