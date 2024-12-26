#include "camera/hal_config.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "sdkconfig.h"
#include <HTTPClient.h>
#include <WiFi.h>

#include <esp_camera.h>
#include <inttypes.h>
#include <nvs_flash.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "PY260 Transfer"
#define WIFI_AUTHMODE WIFI_AUTH_WPA2_PSK
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_SSID "s20154530"
#define WIFI_PASSWORD "Y20154530"

#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 2048

static const int WIFI_RETRY_ATTEMPT = 3;
static int wifi_retry_count = 0;

void InitCamera();

extern "C" void app_main(void) {
    vTaskDelay(3000 / portTICK_PERIOD_MS);
 

    printf("Program Started!\n");
    WiFi.mode(WIFI_STA);
    wl_status_t status = WiFi.begin("CU_eT83","wanglijun123456");
    printf("Init WIFI !\n");
    WiFiClient client;
    client.connect("211.101.235.6",20241,2000);
    printf("Init Camera !\n");
    // InitCamera();
    // esp_restart();
    free(&client);
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void InitCamera() {
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
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        printf("camera init failed\n");
    }
}