#pragma once
#include <string>

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
        framesize_t frameSize = FRAMESIZE_VGA;

    };
} // namespace CONFIG
