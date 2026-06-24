#pragma once

#include <esp_http_server.h>

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

private:
    WebServerService() = default;
    ~WebServerService() = default;

    // HTTP handlers
    static esp_err_t indexHandler(httpd_req_t* req);
    static esp_err_t cssHandler(httpd_req_t* req);
    static esp_err_t jsHandler(httpd_req_t* req);
    static esp_err_t getConfigHandler(httpd_req_t* req);
    static esp_err_t saveConfigHandler(httpd_req_t* req);
    
    // Helper to read file from SPIFFS
    static esp_err_t serveFile(httpd_req_t* req, const char* path, const char* contentType);

    httpd_handle_t _server = nullptr;
};