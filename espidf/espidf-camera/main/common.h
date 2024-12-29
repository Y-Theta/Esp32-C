#pragma once
#include "sdkconfig.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "camera/hal_config.h"

#define TAG                 "PY260 Transfer"
#define WIFI_AUTHMODE       WIFI_AUTH_WPA2_PSK
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

#define WIFI_RETRY_ATTEMPT  3;
#define CONFIG_FILE_PATH    "/config.json"


namespace CONFIG
{
    // Default config
    struct SystemConfig_t
    {
        std::string wifiSsid = "";
        std::string wifiPass = "";
        std::string startPoster = "no";
        std::string waitApFirst = "no";
        std::string nickname = "5mpCamera";
        std::string timeZone = "GMT+0";

        std::string postServer = "127.0.0.1";
        int postPort = 8080;
        int postInterval = 5;

        int jpegQuantity = 12;
        int frameSize = (int)framesize_t::FRAMESIZE_VGA;

    };
} // namespace CONFIG
