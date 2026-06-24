#include "camera/UnitCamS3_5MP.h"
#include "services/StorageService.h"
#include "esp_log.h"

static const char* TAG = "UnitCamS3_5MP";

void UnitCamS3_5MP::SetLed(bool state) {
    gpio_set_level((gpio_num_t)HAL_PIN_LED, state ? 0 : 1);
}

void UnitCamS3_5MP::TakePhoto(std::function<void(camera_fb_t*)> processPhoto) {
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

void UnitCamS3_5MP::StartForSetting() {
    ESP_LOGI(TAG, "StartForSetting called, but not implemented yet");
    while (true) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

void UnitCamS3_5MP::sd_init() {
}

void UnitCamS3_5MP::cam_init() {
    esp_camera_deinit();

    StorageService& storage = StorageService::getInstance();
    const CONFIG::SystemConfig_t& config = storage.getConfig();

    camera_config_t cameraConfig;
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
        ESP_LOGI(TAG, "camera init failed: %s", esp_err_to_name(err));
        return;
    }
    
    // 调整相机参数
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        ESP_LOGI(TAG, "Setting camera parameters...");
        
        // 1. 设置 JPEG 质量 (0-2 for mega_ccm: 0=高, 1=默认, 2=低)
        s->set_quality(s, 1);
        
        // 2. 确保特殊效果为正常（无滤镜）
        s->set_special_effect(s, 0);
        
        // 3. 设置白平衡为自动模式
        s->set_wb_mode(s, 0);
        
        // 4. 调整饱和度 (0-6, 3为默认)
        s->set_saturation(s, 3);
        
        // 5. 调整对比度 (0-6, 3为默认)
        s->set_contrast(s, 3);
        
        // 6. 调整亮度 (0-8, 4为默认)
        s->set_brightness(s, 4);
        
        ESP_LOGI(TAG, "Camera parameters set");
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