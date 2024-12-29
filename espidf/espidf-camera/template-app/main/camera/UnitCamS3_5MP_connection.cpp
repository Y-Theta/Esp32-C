#include "camera/UnitCamS3_5MP.h"

void UnitCamS3_5MP::connect_wifi() {

    ESP_LOGI(TAG,"start wifi STA mode!");
    nvs_flash_init();
    WiFi.mode(WIFI_STA);
    wl_status_t status = WiFi.begin(_config.wifiSsid.c_str(), _config.wifiPass.c_str());
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        wifi_retry_counter++;
        if (wifi_retry_counter >= 60) { // 30 seconds timeout - reset board
            ESP.restart();
        }
    }
}

void UnitCamS3_5MP::disconnect_wifi() {
   esp_wifi_stop();
}

void UnitCamS3_5MP::setup_ap() {
  
}

void UnitCamS3_5MP::close_ap() {
  
}