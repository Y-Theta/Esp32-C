

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_camera.h>
#include <stdio.h>

#include <common.h>


class UnitCamS3_5MP {

protected:
    CONFIG::SystemConfig_t _config;

private:
    int wifi_retry_counter;

    void cam_init();
    void led_init();
    void sd_init();
    void config_init();

    void connect_wifi();
    void disconnect_wifi();

    void setup_ap();
    void close_ap();

public:
    std::function<void()> OnTakePhotoStart;
    std::function<void()> OnTakePhotoEnd;
    std::function<void(camera_fb_t *buffer, UnitCamS3_5MP *camera)> OnProcessImage;

    CONFIG::SystemConfig_t GetConfig();

    void Init();
    void SetLed(bool state);

    void LoadConfig();
    void SaveConfig();

    void TakePhoto(std::function<void(camera_fb_t *buffer)> processPhoto);

    void Start();
    void StartForSetting();
    void StartForWorking();
};
