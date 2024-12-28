#include "hal_config.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include <stdio.h>
#include <Arduino.h>
#include <esp_camera.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <common.h>

class UnitCamS3_5MP {

protected:
    CONFIG::SystemConfig_t _config;

private:
    void cam_init();
    void led_init();
    void sd_init();
    void config_init();

public:
    std::function<void()> OnTakePhotoStart;
    std::function<void()> OnTakePhotoEnd;

    void Init();
    void SetLed(bool state);

    void LoadConfig();
    void SaveConfig();

    void TakePhoto();

    void Start();
    void StartForSetting();
    void StartForWorking();
};
