#include "web_server.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <string>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_http_server.h"

#include "app_state.h"

/*
 * 如果你希望这些配置也独立出去，可以单独做 config.h。
 * 这里为了方便，先放在 web_server.cpp 内部。
 */
#define WIFI_AP_SSID      "LLCC68_Config"

static const char *TAG = "WebServer";

WebServer::WebServer()
{
}

esp_err_t WebServer::start()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.server_port = 80;
    config.stack_size = 8192;

    esp_err_t err = httpd_start(&server_, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t rootUri = {};
    rootUri.uri = "/";
    rootUri.method = HTTP_GET;
    rootUri.handler = WebServer::rootGetHandler;
    rootUri.user_ctx = this;

    httpd_uri_t configUri = {};
    configUri.uri = "/config";
    configUri.method = HTTP_GET;
    configUri.handler = WebServer::configGetHandler;
    configUri.user_ctx = this;

    ESP_ERROR_CHECK(httpd_register_uri_handler(server_, &rootUri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_, &configUri));

    ESP_LOGI(TAG, "Web server started");

    return ESP_OK;
}

esp_err_t WebServer::stop()
{
    if (server_) {
        esp_err_t err = httpd_stop(server_);
        server_ = nullptr;
        return err;
    }

    return ESP_OK;
}

esp_err_t WebServer::rootGetHandler(httpd_req_t *req)
{
    std::string html = renderIndexHtml();

    if (html.empty()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "index.html not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/html; charset=utf-8");

    return httpd_resp_send(
        req,
        html.c_str(),
        html.length()
    );
}

esp_err_t WebServer::configGetHandler(httpd_req_t *req)
{
    char query[512];

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No query");
        return ESP_FAIL;
    }

    AppState& state = AppState::instance();

    LLCC68::Config newCfg = state.getConfig();

    char value[64];

    if (getQueryValue(query, "freq", value, sizeof(value))) {
        newCfg.freqHz = strtoul(value, nullptr, 10);
    }

    if (getQueryValue(query, "power", value, sizeof(value))) {
        newCfg.powerDbm = static_cast<int8_t>(atoi(value));
    }

    if (getQueryValue(query, "sf", value, sizeof(value))) {
        int sf = atoi(value);
        if (sf >= 7 && sf <= 12) {
            newCfg.spreadingFactor = static_cast<uint8_t>(sf);
        }
    }

    if (getQueryValue(query, "bw", value, sizeof(value))) {
        newCfg.bandwidth = parseBandwidth(value);
    }

    if (getQueryValue(query, "cr", value, sizeof(value))) {
        int cr = atoi(value);
        if (cr >= 1 && cr <= 4) {
            newCfg.codingRate = static_cast<uint8_t>(cr);
        }
    }

    if (getQueryValue(query, "sync", value, sizeof(value))) {
        newCfg.syncWord = static_cast<uint16_t>(strtoul(value, nullptr, 0));
    }

    ESP_LOGI(TAG, "New config:");
    ESP_LOGI(TAG, "freq=%lu", static_cast<unsigned long>(newCfg.freqHz));
    ESP_LOGI(TAG, "power=%d", newCfg.powerDbm);
    ESP_LOGI(TAG, "sf=%u", newCfg.spreadingFactor);
    ESP_LOGI(TAG, "bw=0x%02X", newCfg.bandwidth);
    ESP_LOGI(TAG, "cr=%u", newCfg.codingRate);
    ESP_LOGI(TAG, "sync=0x%04X", newCfg.syncWord);

    state.setConfig(newCfg);
    state.setNeedApply(true);

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");

    return httpd_resp_send(req, nullptr, 0);
}

std::string WebServer::renderIndexHtml()
{
    LLCC68::Config cfg = AppState::instance().getConfig();

    std::string html = readFile("/spiffs/index.html");
    if (html.empty()) {
        ESP_LOGE(TAG, "read /spiffs/index.html failed");
        return {};
    }

    replaceAll(html, "{{SSID}}", WIFI_AP_SSID);

    replaceAll(
        html,
        "{{FREQ}}",
        std::to_string(static_cast<unsigned long>(cfg.freqHz))
    );

    replaceAll(
        html,
        "{{POWER}}",
        std::to_string(static_cast<int>(cfg.powerDbm))
    );

    replaceAll(
        html,
        "{{SYNC}}",
        toHex4(cfg.syncWord)
    );

    replaceAll(html, "{{SF7}}",  cfg.spreadingFactor == 7  ? "selected" : "");
    replaceAll(html, "{{SF8}}",  cfg.spreadingFactor == 8  ? "selected" : "");
    replaceAll(html, "{{SF9}}",  cfg.spreadingFactor == 9  ? "selected" : "");
    replaceAll(html, "{{SF10}}", cfg.spreadingFactor == 10 ? "selected" : "");
    replaceAll(html, "{{SF11}}", cfg.spreadingFactor == 11 ? "selected" : "");
    replaceAll(html, "{{SF12}}", cfg.spreadingFactor == 12 ? "selected" : "");

    const char *bw = bandwidthToString(cfg.bandwidth);

    replaceAll(html, "{{BW125}}", strcmp(bw, "125") == 0 ? "selected" : "");
    replaceAll(html, "{{BW250}}", strcmp(bw, "250") == 0 ? "selected" : "");
    replaceAll(html, "{{BW500}}", strcmp(bw, "500") == 0 ? "selected" : "");

    replaceAll(html, "{{CR1}}", cfg.codingRate == 1 ? "selected" : "");
    replaceAll(html, "{{CR2}}", cfg.codingRate == 2 ? "selected" : "");
    replaceAll(html, "{{CR3}}", cfg.codingRate == 3 ? "selected" : "");
    replaceAll(html, "{{CR4}}", cfg.codingRate == 4 ? "selected" : "");

    return html;
}

bool WebServer::getQueryValue(
    const char *query,
    const char *key,
    char *value,
    size_t valueLen
)
{
    if (!query || !key || !value) {
        return false;
    }

    return httpd_query_key_value(query, key, value, valueLen) == ESP_OK;
}

uint8_t WebServer::parseBandwidth(const char *str)
{
    if (strcmp(str, "125") == 0) {
        return 0x04;
    }

    if (strcmp(str, "250") == 0) {
        return 0x05;
    }

    if (strcmp(str, "500") == 0) {
        return 0x06;
    }

    return 0x04;
}

const char *WebServer::bandwidthToString(uint8_t bw)
{
    switch (bw) {
        case 0x04:
            return "125";

        case 0x05:
            return "250";

        case 0x06:
            return "500";

        default:
            return "125";
    }
}

std::string WebServer::readFile(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        ESP_LOGE(TAG, "fopen failed: %s", path);
        return {};
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return {};
    }

    long len = ftell(f);
    if (len <= 0) {
        fclose(f);
        return {};
    }

    rewind(f);

    std::string content;
    content.resize(static_cast<size_t>(len));

    size_t readLen = fread(
        &content[0],
        1,
        static_cast<size_t>(len),
        f
    );

    fclose(f);

    if (readLen != static_cast<size_t>(len)) {
        ESP_LOGW(TAG, "file read incomplete: %u/%ld", static_cast<unsigned>(readLen), len);
    }

    return content;
}

void WebServer::replaceAll(
    std::string& s,
    const std::string& from,
    const std::string& to
)
{
    if (from.empty()) {
        return;
    }

    size_t pos = 0;

    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.length(), to);
        pos += to.length();
    }
}

std::string WebServer::toHex4(uint16_t value)
{
    char buf[16];

    snprintf(
        buf,
        sizeof(buf),
        "0x%04X",
        value
    );

    return std::string(buf);
}