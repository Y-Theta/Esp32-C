#include "camera/UnitCamS3_5MP.h"
#include "services/StorageService.h"
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

    camera_fb_t* fb = nullptr;

    if (OnTakePhotoStart != nullptr) {
        OnTakePhotoStart();
    }

    SetLed(true);

    fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGI(TAG, "Failed to get camera frame");
        SetLed(false);
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
    fb = nullptr;
    
    if (OnTakePhotoEnd != nullptr) {
        OnTakePhotoEnd();
    }
    
    SetLed(false);
}



void UnitCamS3_5MP::sd_init() {
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
    cameraConfig.xclk_freq_hz = XCLK_FREQ_HZ;
    cameraConfig.pixel_format = PIXFORMAT_JPEG;
    cameraConfig.frame_size = (framesize_t)config.frameSize;
    cameraConfig.jpeg_quality = 8;
    cameraConfig.fb_count = 1;
    cameraConfig.fb_location = CAMERA_FB_IN_PSRAM;
    cameraConfig.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    cameraConfig.sccb_i2c_port = 1;

    esp_err_t err = esp_camera_init(&cameraConfig);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "camera init failed: %s", esp_err_to_name(err));
        _initialized = false;
        return;
    }
    _initialized = true;
    
    // 调整相机参数
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        ESP_LOGI(TAG, "Setting camera parameters...");

        // 0. 设置 Frame Size (关键修复！)
        s->set_framesize(s, (framesize_t)config.frameSize);

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

        ESP_LOGI(TAG, "Camera parameters set - frameSize=%d, jpegQuality=%d, wb=%d, contrast=%d, saturation=%d, brightness=%d, effect=%d",
                 config.frameSize, jpegQuality, config.wbMode, config.contrast, config.saturation, config.brightness, config.specialEffect);
    }
    
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "camera ready after stabilization");
}

void UnitCamS3_5MP::led_init() {
    gpio_reset_pin((gpio_num_t)HAL_PIN_LED);
    gpio_set_direction((gpio_num_t)HAL_PIN_LED, GPIO_MODE_OUTPUT);
    gpio_set_pull_mode((gpio_num_t)HAL_PIN_LED, GPIO_PULLUP_ONLY);
}

void UnitCamS3_5MP::mic_init() {
    ESP_LOGI(TAG, "init mic");
}

static void btn_thread(void *parameter) {
    while (1) {
        if (gpio_get_level((gpio_num_t)BTN_0)) {
            ESP_LOGI(TAG, "Btn Typed ");
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

void UnitCamS3_5MP::btn_init() {
    ESP_LOGI(TAG, "init btn");
    gpio_reset_pin((gpio_num_t)BTN_0);
    gpio_set_direction((gpio_num_t)BTN_0, GPIO_MODE_INPUT);
    gpio_set_pull_mode((gpio_num_t)BTN_0, GPIO_PULLUP_ONLY);

    xTaskCreate(btn_thread, "btn", 256, NULL, 6, NULL);
}

void UnitCamS3_5MP::ReloadConfig() {
    ESP_LOGI(TAG, "Reloading camera config...");
    if (_initialized) {
        esp_camera_deinit();
        _initialized = false;
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

void UnitCamS3_5MP::ApplyCameraConfig() {
    ReloadConfig();
}

void UnitCamS3_5MP::Init() {
    // 初始化存储服务
    StorageService& storage = StorageService::getInstance();
    storage.init();
    
    // 如果配置为空，设置默认值
    if (storage.getConfig().wifiSsid.empty()) {
        CONFIG::SystemConfig_t defaultConfig;
        defaultConfig.wifiSsid = "s20154530";
        defaultConfig.wifiPass = "Y20154530";
        defaultConfig.postServer = "n8n.y-theta.cn";
        defaultConfig.postPort = 443;
        storage.setConfig(defaultConfig);
    }

    led_init();
    sd_init();
}