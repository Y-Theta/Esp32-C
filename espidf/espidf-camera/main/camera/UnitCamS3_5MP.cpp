#include "camera/UnitCamS3_5MP.h"

void UnitCamS3_5MP::sd_init() {
}

void UnitCamS3_5MP::cam_init() {

    esp_camera_deinit();

    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = CAMERA_PIN_D0;
    config.pin_d1 = CAMERA_PIN_D1;
    config.pin_d2 = CAMERA_PIN_D2;
    config.pin_d3 = CAMERA_PIN_D3;
    config.pin_d4 = CAMERA_PIN_D4;
    config.pin_d5 = CAMERA_PIN_D5;
    config.pin_d6 = CAMERA_PIN_D6;
    config.pin_d7 = CAMERA_PIN_D7;
    config.pin_xclk = CAMERA_PIN_XCLK;
    config.pin_pclk = CAMERA_PIN_PCLK;
    config.pin_vsync = CAMERA_PIN_VSYNC;
    config.pin_href = CAMERA_PIN_HREF;
    config.pin_sccb_sda = CAMERA_PIN_SIOD;
    config.pin_sccb_scl = CAMERA_PIN_SIOC;
    config.pin_pwdn = CAMERA_PIN_PWDN;
    config.pin_reset = CAMERA_PIN_RESET;
    config.xclk_freq_hz = XCLK_FREQ_HZ;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = (framesize_t)_config.frameSize;
    config.jpeg_quality = _config.jpegQuantity;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        printf("camera init failed\n");
    }
}

void UnitCamS3_5MP::config_init() {
    if (!LittleFS.begin(true)) {
        ESP_LOGI(TAG, "littlefs init failed");
    }
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
        };
        vTaskDelay(10);
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
    config_init();
    cam_init();
    led_init();
    sd_init();

    _config.wifiSsid = "s20154530";
    _config.wifiPass = "Y20154530";

    _config.postServer = "38.147.174.195";
    _config.postPort = 20678;
}