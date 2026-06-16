#pragma once

#include <string>

#include "esp_err.h"
#include "esp_http_server.h"

class WebServer {
public:
    WebServer();

    esp_err_t start();
    esp_err_t stop();

private:
    httpd_handle_t server_ = nullptr;

private:
    static esp_err_t rootGetHandler(httpd_req_t *req);
    static esp_err_t styleGetHandler(httpd_req_t *req);
    static esp_err_t appJsGetHandler(httpd_req_t *req);

    static esp_err_t apiConfigGetHandler(httpd_req_t *req);
    static esp_err_t apiConfigPostHandler(httpd_req_t *req);

    static esp_err_t apiSendPostHandler(httpd_req_t *req);
    static esp_err_t apiReceiveGetHandler(httpd_req_t *req);

private:
    static esp_err_t sendFile(
        httpd_req_t *req,
        const char *path,
        const char *contentType
    );

    static bool readRequestBody(
        httpd_req_t *req,
        std::string& out
    );

    static uint8_t parseBandwidth(const char *str);
    static const char *bandwidthToString(uint8_t bw);

    static std::string readFile(const char *path);
    static std::string toHex4(uint16_t value);

    /*
     * 不使用 cJSON，手写简单 JSON 辅助函数
     */
    static bool jsonGetString(
        const std::string& json,
        const char *key,
        std::string& out
    );

    static bool jsonGetInt(
        const std::string& json,
        const char *key,
        int& out
    );

    static std::string jsonEscape(
        const std::string& s
    );
};