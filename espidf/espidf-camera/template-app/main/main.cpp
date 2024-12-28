#include "camera/hal_config.h"
#include "camera/UnitCamS3_5MP.h"

#include <HTTPClient.h>
#include <WiFi.h>

#define WIFI_SSID "CU_eT83"
#define WIFI_PASSWORD "wanglijun123456"

static int wifi_retry_count = 0;

void InitCamera();
void takePhoto(void *parameters);
void setLedState(bool state);
void InitLED();
int counter = 0;

extern "C" void app_main(void) {
    vTaskDelay(3000 / portTICK_PERIOD_MS);

    UnitCamS3_5MP* camera = new UnitCamS3_5MP();
    camera->Init();

    // nvs_flash_init();
    // printf("Program Started!\n");
    // WiFi.mode(WIFI_STA);
    // wl_status_t status = WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    // printf("Init WIFI !\n");
    // while (WiFi.status() != WL_CONNECTED) {
    //     delay(500);
    //     counter++;
    //     if (counter >= 60) { // 30 seconds timeout - reset board
    //         ESP.restart();
    //     }
    // }
    // // delete client;
    // printf("Init Camera !\n");
    // InitCamera();
    // InitLED();
    // // esp_restart();
    // xTaskCreate(takePhoto, "photo", 5 * 1024, NULL, 5, NULL);
    while (1) {
        vTaskDelay(1000);
    }
}

// String boundary = "-------------------------645436246131408991458787";
// String constpostinfo = "--" + boundary + "\r\n" +
//                        "Content-Disposition: form-data; name=\"image\"; filename=\"'photo.jpeg'\"\r\n" +
//                        "Content-Type: image/jpeg\r\n\r\n";
// String footer = "\r\n--" + boundary + "--\r\n";

// void uploadPhoto(WiFiClient *client, camera_fb_t *fb) {

//     if (!client->connected()) {
//         if (!client->connect("38.147.174.195", 20678)) {
//             printf("Connection failed!");
//             return;
//         }
//     }

//     printf("start uploading !");
//     int contentLength = constpostinfo.length() + fb->len + footer.length();
//     client->print("POST /imgup HTTP/1.1\r\n");
//     client->println("Accept: */*");
//     client->print("Host: 38.147.174.195:20678\r\n");
//     client->println("Connection: Keep-Alive");
//     client->print("Content-Type: multipart/form-data; boundary=" + boundary + "\r\n");
//     client->print("Content-Length: " + String(contentLength) + "\r\n\r\n");
//     client->print(constpostinfo);
//     client->write(fb->buf, fb->len);
//     client->print(footer);
// }

// void takePhoto(void *parameters) {
//     uint32_t post_time_count = 0;
//     bool start_post = true;
//     WiFiClient *client = new WiFiClient();
//     if (!client->connect("38.147.174.195", 20678)) {
//         printf("Connection failed!");
//         return;
//     }

//     while (true) {
//         delay(100);

//         camera_fb_t *fb = NULL;
//         setLedState(1);
//         fb = esp_camera_fb_get();
//         if (!fb) {
//         setLedState(0);
//             vTaskDelay(4000);
//             continue;
//         }
//         ESP_LOGI(TAG, "captrued height: %d , width: %d, length: %d", fb->height, fb->width, fb->len);
//         uploadPhoto(client, fb);
//         esp_camera_fb_return(fb);
//         fb = NULL;
//         setLedState(0);
//         vTaskDelay(5 * 1000);
//     }
// }
