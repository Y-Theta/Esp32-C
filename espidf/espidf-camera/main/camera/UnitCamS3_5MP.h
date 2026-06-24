#pragma once

#include <esp_camera.h>
#include <string>
#include <functional>

class UnitCamS3_5MP {
private:
    void cam_init();
    void led_init();
    void sd_init();
    void mic_init();
    void btn_init();

public:
    std::function<void()> OnTakePhotoStart;
    std::function<void()> OnTakePhotoEnd;
    std::function<void(int gpio)> OnBtnClick;
    std::function<void(camera_fb_t* buffer, UnitCamS3_5MP* camera)> OnProcessImage;

    void Init();
    void SetLed(bool state);

    void TakePhoto(std::function<void(camera_fb_t* buffer)> processPhoto);

    void Start();
    void StartForSetting();
    void StartForWorking();
};