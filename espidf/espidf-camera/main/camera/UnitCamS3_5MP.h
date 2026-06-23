#include <esp_camera.h>
#include <esp_wifi.h>
#include <esp_http_client.h>
#include <cJSON.h>
#include <esp_vfs.h>
#include <string>
#include <functional>

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
    void mic_init();
    void btn_init();

    void connect_wifi();
    void disconnect_wifi();

    void setup_ap();
    void close_ap();

public:
    std::function<void()> OnTakePhotoStart;
    std::function<void()> OnTakePhotoEnd;
    std::function<void(int gpio)> OnBtnClick;
    std::function<void(camera_fb_t *buffer, UnitCamS3_5MP *camera)> OnProcessImage;

    CONFIG::SystemConfig_t GetConfig();
    void SetConfig(CONFIG::SystemConfig_t config, CONFIG::ConfigType type);

    void Init();
    void SetLed(bool state);

    void LoadConfig();
    void SaveConfig();

    void TakePhoto(std::function<void(camera_fb_t *buffer)> processPhoto);

    void Start();
    void StartForSetting();
    void StartForWorking();
};