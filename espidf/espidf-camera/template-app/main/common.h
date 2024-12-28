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
        std::string nickname = "UnitCamS3";
        std::string timeZone = "GMT+0";
        int postInterval = 5;
    };
} // namespace CONFIG
