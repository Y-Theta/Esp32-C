#pragma once
#include "esp_camera.h"
#include <string>

#include "camera/hal_config.h"

#define CONFIG_FILE_PATH "/spiffs/config.json"

#define MAX_WIFI_SSIDS 3

namespace CONFIG {

enum BootMode {
    BOOT_MODE_INTERACTIVE = 0,
    BOOT_MODE_MONITOR = 1
};

struct SystemConfig_t {
    std::string wifiSsid[MAX_WIFI_SSIDS] = {"", "", ""};
    std::string wifiPass[MAX_WIFI_SSIDS] = {"", "", ""};
    
    std::string postServer = "";
    bool postUsePut = false;
    int bootMode = BOOT_MODE_INTERACTIVE;
    int uploadInterval = 60;

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