#pragma once

#include <esp_camera.h>
#include <string>
#include <functional>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_http_client.h"

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
    std::function<void()> onBeforeReload;

    void Init();
    void SetLed(bool state);

    void TakePhoto(std::function<void(camera_fb_t* buffer)> processPhoto = nullptr);

    void Start();
    void StartForSetting();
    void StartForMonitor();

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

    // 公开锁函数，供WebServer推流任务使用
    void _lockCameraFunc() { lockCamera(); }
    void _unlockCameraFunc() { unlockCamera(); }
    
    // RAII锁守卫，自动管理加锁解锁，确保任何路径都能释放锁
    class CameraLockGuard {
    public:
        explicit CameraLockGuard(UnitCamS3_5MP& camera) : _camera(camera) {
            _camera.lockCamera();
            _locked = true;
        }
        ~CameraLockGuard() {
            if (_locked) {
                _camera.unlockCamera();
                _locked = false;
            }
        }
        // 禁止拷贝
        CameraLockGuard(const CameraLockGuard&) = delete;
        CameraLockGuard& operator=(const CameraLockGuard&) = delete;
        // 支持提前解锁
        void unlock() {
            if (_locked) {
                _camera.unlockCamera();
                _locked = false;
            }
        }
    private:
        UnitCamS3_5MP& _camera;
        bool _locked;
    };

private:
    UnitCamS3_5MP() = default;
    ~UnitCamS3_5MP() = default;

    void cam_init();
    void led_init();
    static void monitorTask(void* arg);
    void uploadPhoto(camera_fb_t* fb);
    static esp_err_t httpEventHandler(esp_http_client_event_t* evt);
    
    // 相机互斥锁，保护相机初始化、拍照、参数设置操作（递归锁支持嵌套调用）
    SemaphoreHandle_t _cameraMutex = nullptr;
    bool _lockInitialized = false;
    
    void lockCamera();
    void unlockCamera();

    bool _initialized = false;
    bool _streamingMode = false;
    bool _takePhotoActive = false;
    bool _monitorModeRunning = false;
    
    static const char* _httpBoundary;
    char _postHeader[512];
    char _postFooter[32];
};