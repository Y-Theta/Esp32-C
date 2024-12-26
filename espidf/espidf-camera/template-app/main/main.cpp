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
void takePhoto(void *parameters);
int counter = 0;

extern "C" void app_main(void) {
    vTaskDelay(3000 / portTICK_PERIOD_MS);

    nvs_flash_init();
    InitCamera();

    printf("Program Started!\n");
    WiFi.mode(WIFI_STA);
    wl_status_t status = WiFi.begin("s20154530", "Y20154530");
    printf("Init WIFI !\n");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        counter++;
        if (counter >= 60) { // 30 seconds timeout - reset board
            ESP.restart();
        }
    }

    // printf("Connecting to website: ");
    // HTTPClient *client = new HTTPClient();
    // client->begin("https://api.y-theta.cn/firefly");
    // int httpCode = client->GET();
    // if (httpCode > 0) {
    //     ESP_LOGI(TAG, "[HTTP] GET... code: %d\n", httpCode);
    //     // file found at server --> on unsuccessful connection code will be -1
    //     if (httpCode == HTTP_CODE_OK) {
    //         String payload = client->getString();
    //         ESP_LOGI(TAG, "%s", payload.c_str());
    //     }
    // } else {
    //     ESP_LOGI(TAG, "[HTTP] GET... failed, error: %s\n", client->errorToString(httpCode).c_str());
    // }

    // delete client;
    printf("Init Camera !\n");
    // esp_restart();
    xTaskCreate(takePhoto, "photo", 5 * 1024, NULL, 5, NULL);
    while (1) {
        vTaskDelay(1000);
    }
}


void takePhoto(void *parameters) {
    uint32_t post_time_count = 0;
    bool start_post = true;
    int post_count_down = 1000;

    while (true) {
        delay(100);

        // if (millis() - post_time_count > 1000) {
            // if (start_post) {
                start_post = false;

                camera_fb_t *fb = NULL;
                fb = esp_camera_fb_get();
                if (!fb) {
                    vTaskDelay(4000);
                    continue;
                }
                vTaskDelay(1000);
                ESP_LOGI(TAG, "captrued height: %d , width: %d, length: %d", fb->height, fb->width, fb->len);

                esp_camera_fb_return(fb);
                fb = NULL;
                post_count_down = 1000;
            // }

        //     post_count_down--;
        //     if (post_count_down <= 0)
        //         start_post = true;

        //     // spdlog::info("post count down: {}", post_count_down);
        //     post_time_count = millis();
        // }
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
    config.jpeg_quality = 10;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        printf("camera init failed\n");
    }
}
