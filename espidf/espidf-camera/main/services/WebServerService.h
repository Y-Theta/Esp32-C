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
    
    // 回调函数
    std::function<void()> onTakePhotoRequested;
    std::function<void()> onConnectToSTRequested;
    std::function<void()> onDisconnectWiFiRequested;
    
    // 设置相机回调
    void setCameraInstance(class UnitCamS3_5MP* camera) { _camera = camera; }

private:
    WebServerService() = default;
    ~WebServerService() = default;

    // HTTP handlers
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
    
    // Helper to read file from SPIFFS
    static esp_err_t serveFile(httpd_req_t* req, const char* path, const char* contentType);

    httpd_handle_t _server = nullptr;
    UnitCamS3_5MP* _camera = nullptr;
};