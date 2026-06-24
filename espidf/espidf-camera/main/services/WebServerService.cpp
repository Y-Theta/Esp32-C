#include "services/WebServerService.h"
#include "services/StorageService.h"
#include "esp_log.h"
#include "cJSON.h"
#include <esp_system.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* TAG = "WebServerService";

esp_err_t WebServerService::serveFile(httpd_req_t* req, const char* path, const char* contentType) {
    ESP_LOGI(TAG, "Serving file: %s", path);
    
    FILE* file = fopen(path, "r");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file: %s", path);
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_sendstr(req, "File not found");
        return ESP_FAIL;
    }
    
    fseek(file, 0, SEEK_END);
    long fsize = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char* buffer = new char[fsize + 1];
    fread(buffer, 1, fsize, file);
    buffer[fsize] = '\0';
    fclose(file);
    
    httpd_resp_set_type(req, contentType);
    httpd_resp_send(req, buffer, fsize);
    delete[] buffer;
    
    return ESP_OK;
}

esp_err_t WebServerService::indexHandler(httpd_req_t* req) {
    return serveFile(req, "/spiffs/index.html", "text/html");
}

esp_err_t WebServerService::cssHandler(httpd_req_t* req) {
    return serveFile(req, "/spiffs/style.css", "text/css");
}

esp_err_t WebServerService::jsHandler(httpd_req_t* req) {
    return serveFile(req, "/spiffs/app.js", "application/javascript");
}

esp_err_t WebServerService::getConfigHandler(httpd_req_t* req) {
    ESP_LOGI(TAG, "Getting config");
    
    StorageService& storage = StorageService::getInstance();
    const CONFIG::SystemConfig_t& config = storage.getConfig();
    
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "wifiSsid", config.wifiSsid.c_str());
    cJSON_AddStringToObject(root, "wifiPass", config.wifiPass.c_str());
    cJSON_AddStringToObject(root, "postServer", config.postServer.c_str());
    cJSON_AddNumberToObject(root, "postPort", config.postPort);
    cJSON_AddNumberToObject(root, "postInterval", config.postInterval);
    cJSON_AddNumberToObject(root, "jpegQuantity", config.jpegQuantity);
    cJSON_AddNumberToObject(root, "frameSize", config.frameSize);
    cJSON_AddStringToObject(root, "startPoster", config.startPoster.c_str());
    cJSON_AddStringToObject(root, "waitApFirst", config.waitApFirst.c_str());
    cJSON_AddStringToObject(root, "nickname", config.nickname.c_str());
    cJSON_AddStringToObject(root, "timeZone", config.timeZone.c_str());
    
    char* json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    
    free(json_str);
    cJSON_Delete(root);
    
    return ESP_OK;
}

esp_err_t WebServerService::saveConfigHandler(httpd_req_t* req) {
    ESP_LOGI(TAG, "Saving config");
    
    char* buf = new char[req->content_len + 1];
    int ret = httpd_req_recv(req, buf, req->content_len);
    if (ret <= 0) {
        delete[] buf;
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "Failed to receive data");
        return ESP_FAIL;
    }
    buf[req->content_len] = '\0';
    
    cJSON* root = cJSON_Parse(buf);
    delete[] buf;
    
    if (!root) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "Invalid JSON");
        return ESP_FAIL;
    }
    
    StorageService& storage = StorageService::getInstance();
    CONFIG::SystemConfig_t config = storage.getConfig();
    
    cJSON* item = cJSON_GetObjectItem(root, "wifiSsid");
    if (item && cJSON_IsString(item)) config.wifiSsid = item->valuestring;
    
    item = cJSON_GetObjectItem(root, "wifiPass");
    if (item && cJSON_IsString(item)) config.wifiPass = item->valuestring;
    
    item = cJSON_GetObjectItem(root, "postServer");
    if (item && cJSON_IsString(item)) config.postServer = item->valuestring;
    
    item = cJSON_GetObjectItem(root, "postPort");
    if (item && cJSON_IsNumber(item)) config.postPort = item->valueint;
    
    item = cJSON_GetObjectItem(root, "postInterval");
    if (item && cJSON_IsNumber(item)) config.postInterval = item->valueint;
    
    item = cJSON_GetObjectItem(root, "jpegQuantity");
    if (item && cJSON_IsNumber(item)) config.jpegQuantity = item->valueint;
    
    item = cJSON_GetObjectItem(root, "frameSize");
    if (item && cJSON_IsNumber(item)) config.frameSize = item->valueint;
    
    item = cJSON_GetObjectItem(root, "startPoster");
    if (item && cJSON_IsString(item)) config.startPoster = item->valuestring;
    
    item = cJSON_GetObjectItem(root, "waitApFirst");
    if (item && cJSON_IsString(item)) config.waitApFirst = item->valuestring;
    
    item = cJSON_GetObjectItem(root, "nickname");
    if (item && cJSON_IsString(item)) config.nickname = item->valuestring;
    
    item = cJSON_GetObjectItem(root, "timeZone");
    if (item && cJSON_IsString(item)) config.timeZone = item->valuestring;
    
    cJSON_Delete(root);
    
    storage.setConfig(config);
    storage.save();
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    
    ESP_LOGI(TAG, "Config saved, restarting...");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    esp_restart();
    
    return ESP_OK;
}

void WebServerService::start() {
    if (_server) {
        ESP_LOGI(TAG, "Server already running");
        return;
    }
    
    ESP_LOGI(TAG, "Starting web server...");
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.uri_match_fn = httpd_uri_match_wildcard;
    
    if (httpd_start(&_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server");
        return;
    }
    
    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = indexHandler,
        .user_ctx = nullptr
    };
    
    httpd_uri_t css_uri = {
        .uri = "/style.css",
        .method = HTTP_GET,
        .handler = cssHandler,
        .user_ctx = nullptr
    };
    
    httpd_uri_t js_uri = {
        .uri = "/app.js",
        .method = HTTP_GET,
        .handler = jsHandler,
        .user_ctx = nullptr
    };
    
    httpd_uri_t get_config_uri = {
        .uri = "/api/config",
        .method = HTTP_GET,
        .handler = getConfigHandler,
        .user_ctx = nullptr
    };
    
    httpd_uri_t save_config_uri = {
        .uri = "/api/config",
        .method = HTTP_POST,
        .handler = saveConfigHandler,
        .user_ctx = nullptr
    };
    
    httpd_register_uri_handler(_server, &index_uri);
    httpd_register_uri_handler(_server, &css_uri);
    httpd_register_uri_handler(_server, &js_uri);
    httpd_register_uri_handler(_server, &get_config_uri);
    httpd_register_uri_handler(_server, &save_config_uri);
    
    ESP_LOGI(TAG, "Web server started");
}

void WebServerService::stop() {
    if (_server) {
        httpd_stop(_server);
        _server = nullptr;
        ESP_LOGI(TAG, "Web server stopped");
    }
}