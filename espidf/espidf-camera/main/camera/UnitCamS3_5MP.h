#pragma once

#include <esp_camera.h>
#include <string>
#include <functional>

class UnitCamS3_5MP {
public:
    static UnitCamS3_5MP& getInstance() {
        static UnitCamS3_5MP instance;
        return instance;
    }

    UnitCamS3_5MP(const UnitCamS3_5MP&) = delete;
    UnitCamS3_5MP& operator=(const UnitCamS3_5MP&) = delete;

    std::function<void()> OnTakePhotoStart;
    std::function<void()> OnTakePhotoEnd;
    std::function<void(camera_fb_t* buffer, UnitCamS3_5MP* camera)> OnProcessImage;
    std::function<void()> onBeforeReload;

    void Init();
    void SetLed(bool state);

    void TakePhoto(std::function<void(camera_fb_t* buffer)> processPhoto = nullptr);

    void Start();
    void StartForSetting();

    void WebTakePhoto(std::function<void(camera_fb_t* buffer)> processPhoto = nullptr);

    bool IsInitialized() const { return _initialized; }
    void ReloadConfig();
    void SetFrameSize(framesize_t frameSize);
    void SetJpegQuality(int quality);
    void SetWhiteBalance(int wbMode);
    void SetContrast(int contrast);
    void SetSaturation(int saturation);
    void SetBrightness(int brightness);
    void SetSpecialEffect(int effect);
    void ApplyCameraConfig();
    void SetAllCameraConfig(int frameSize, int jpegQuality, int wbMode, int specialEffect, int contrast, int saturation, int brightness);

    void StartStreamingMode();
    void StopStreamingMode();
    bool IsStreamingMode() const { return _streamingMode; }

private:
    UnitCamS3_5MP() = default;
    ~UnitCamS3_5MP() = default;

    void cam_init();
    void led_init();

    bool _initialized = false;
    bool _streamingMode = false;
    bool _takePhotoActive = false;
};