#include "camera/UnitCamS3_5MP.h"

#include <WiFiClient.h>

#define WIFI_SSID "CU_eT83"
#define WIFI_PASSWORD "wanglijun123456"

UnitCamS3_5MP *camera = nullptr;

namespace Operation {

static String boundary = "-------562164BDF";
static String constpostinfo = "--" + boundary + "\r\n" +
                              "Content-Disposition: form-data; name=\"image\"; filename=\"'photo.jpeg'\"\r\n" +
                              "Content-Type: image/jpeg\r\n\r\n";
static String footer = "\r\n--" + boundary + "--\r\n";

static void upload_photo(camera_fb_t *fb, UnitCamS3_5MP *ump) {
    const char *server = ump->GetConfig().postServer.c_str();
    int port = ump->GetConfig().postPort;

    WiFiClient *client = new WiFiClient();
    if (!client->connect(server, port)) {
        ESP_LOGI(TAG, "Connection failed!");
        return;
    }

    ESP_LOGI(TAG, "start uploading !");
    int contentLength = constpostinfo.length() + fb->len + footer.length();
    client->print("POST /imgup HTTP/1.1\r\n");
    client->println("Accept: */*");
    client->print("Host: " + String(server) + ":" + String(port) + "\r\n");
    client->println("Connection: Keep-Alive");
    client->print("Content-Type: multipart/form-data; boundary=" + boundary + "\r\n");
    client->print("Content-Length: " + String(contentLength) + "\r\n\r\n");
    client->print(constpostinfo);
    client->write(fb->buf, fb->len);
    client->print(footer);
    delete client;
}

} // namespace Operation

extern "C" void app_main(void) {
    vTaskDelay(3000 / portTICK_PERIOD_MS);

    camera = new UnitCamS3_5MP();
    camera->OnProcessImage = Operation::upload_photo;
    camera->Init();
    camera->StartForWorking();
}
