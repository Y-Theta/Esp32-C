#pragma once
#include "esp_camera.h"
#include <string>

#include "camera/hal_config.h"

#define CONFIG_FILE_PATH "/spiffs/config.json"

namespace CONFIG {
struct SystemConfig_t {
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
    int frameSize = (int)FRAMESIZE_VGA;
    int streamFps = 25;
    int streamFrameSize = (int)FRAMESIZE_VGA;
    int streamFbCount = 2;

    int wbMode = 0;
    int contrast = 3;
    int saturation = 3;
    int brightness = 4;
    int specialEffect = 0;
};
} // namespace CONFIG