#pragma once
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "esp_camera.h"
#include <string>

#include "camera/hal_config.h"

// 避免 TAG 宏冲突，不要在全局定义 TAG
#define WIFI_AUTHMODE WIFI_AUTH_WPA2_PSK
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

#define WIFI_RETRY_ATTEMPT 3
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

    // 相机图像参数
    int wbMode = 0;       // 白平衡: Auto=0, sunny=1, office=2, cloudy=3, home=4
    int contrast = 3;     // 对比度: 0-6, 3 默认
    int saturation = 3;   // 饱和度: 0-6, 3 默认
    int brightness = 4;   // 亮度: 0-8, 4 默认
    int specialEffect = 0; // 特效: 0=正常
};

enum ConfigType {
    APP = 0x01,
    WIFI = 0x02,
    CAMERA = 0x04
};
} // namespace CONFIG