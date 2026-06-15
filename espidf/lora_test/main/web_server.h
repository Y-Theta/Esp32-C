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
    static esp_err_t rootGetHandler(httpd_req_t *req);
    static esp_err_t configGetHandler(httpd_req_t *req);

    static std::string renderIndexHtml();

    static bool getQueryValue(
        const char *query,
        const char *key,
        char *value,
        size_t valueLen
    );

    static uint8_t parseBandwidth(const char *str);
    static const char *bandwidthToString(uint8_t bw);

    static std::string readFile(const char *path);
    static void replaceAll(
        std::string& s,
        const std::string& from,
        const std::string& to
    );

    static std::string toHex4(uint16_t value);

private:
    httpd_handle_t server_ = nullptr;
};