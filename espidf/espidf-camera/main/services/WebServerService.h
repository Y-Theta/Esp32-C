#pragma once

#include <esp_http_server.h>
#include <functional>
#include <esp_camera.h>

class WebServerService {
public:
    static WebServerService& getInstance() {
        static WebServerService instance;
        return instance;
    }

    WebServerService(const WebServerService&) = delete;
    WebServerService& operator=(const WebServerService&) = delete;

    void start();
    void stop();
    bool isRunning() const { return _server != nullptr; }

    httpd_handle_t getServerHandle() const { return _server; }

    // 拍照完成回调，由相机模块调用
    static void notifyPhotoCaptured(camera_fb_t* fb);

    // 回调函数
    std::function<void()> onTakePhotoRequested;
    std::function<void()> onConnectToSTRequested;
    std::function<void()> onDisconnectWiFiRequested;

    // HTTP handlers - 必须为 public，供文件作用域 URI 结构体引用
    static esp_err_t indexHandler(httpd_req_t* req);
    static esp_err_t cssHandler(httpd_req_t* req);
    static esp_err_t jsHandler(httpd_req_t* req);
    static esp_err_t getConfigHandler(httpd_req_t* req);
    static esp_err_t saveConfigHandler(httpd_req_t* req);

    // API handlers
    static esp_err_t apiTakePhotoHandler(httpd_req_t* req);
    static esp_err_t apiConnectSTAHandler(httpd_req_t* req);
    static esp_err_t apiGetStatusHandler(httpd_req_t* req);
    static esp_err_t apiLastPhotoHandler(httpd_req_t* req);
    static esp_err_t apiPhotoStatusHandler(httpd_req_t* req);
    static esp_err_t apiMemoryStatusHandler(httpd_req_t* req);
    static esp_err_t apiSetAllCameraConfigHandler(httpd_req_t* req);
    static esp_err_t apiCameraStatusHandler(httpd_req_t* req);
    static esp_err_t apiStreamStartHandler(httpd_req_t* req);
    static esp_err_t apiStreamStopHandler(httpd_req_t* req);
    static esp_err_t apiStreamFrameHandler(httpd_req_t* req);

    // Helper to read file from SPIFFS
    static esp_err_t serveFile(httpd_req_t* req, const char* path, const char* contentType);

private:
    WebServerService() = default;
    ~WebServerService() = default;

    httpd_handle_t _server = nullptr;
};