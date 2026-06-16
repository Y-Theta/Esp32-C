#include "web_server.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include <string>
#include <vector>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_http_server.h"

#include "app_state.h"

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

    /*
     * 静态页面
     */
    httpd_uri_t rootUri = {};
    rootUri.uri = "/";
    rootUri.method = HTTP_GET;
    rootUri.handler = WebServer::rootGetHandler;
    rootUri.user_ctx = this;

    httpd_uri_t styleUri = {};
    styleUri.uri = "/style.css";
    styleUri.method = HTTP_GET;
    styleUri.handler = WebServer::styleGetHandler;
    styleUri.user_ctx = this;

    httpd_uri_t appJsUri = {};
    appJsUri.uri = "/app.js";
    appJsUri.method = HTTP_GET;
    appJsUri.handler = WebServer::appJsGetHandler;
    appJsUri.user_ctx = this;

    /*
     * JSON API
     */
    httpd_uri_t apiConfigGetUri = {};
    apiConfigGetUri.uri = "/api/config";
    apiConfigGetUri.method = HTTP_GET;
    apiConfigGetUri.handler = WebServer::apiConfigGetHandler;
    apiConfigGetUri.user_ctx = this;

    httpd_uri_t apiConfigPostUri = {};
    apiConfigPostUri.uri = "/api/config";
    apiConfigPostUri.method = HTTP_POST;
    apiConfigPostUri.handler = WebServer::apiConfigPostHandler;
    apiConfigPostUri.user_ctx = this;

    httpd_uri_t apiSendUri = {};
    apiSendUri.uri = "/api/send";
    apiSendUri.method = HTTP_POST;
    apiSendUri.handler = WebServer::apiSendPostHandler;
    apiSendUri.user_ctx = this;

    httpd_uri_t apiReceiveUri = {};
    apiReceiveUri.uri = "/api/receive";
    apiReceiveUri.method = HTTP_GET;
    apiReceiveUri.handler = WebServer::apiReceiveGetHandler;
    apiReceiveUri.user_ctx = this;

    ESP_ERROR_CHECK(httpd_register_uri_handler(server_, &rootUri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_, &styleUri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_, &appJsUri));

    ESP_ERROR_CHECK(httpd_register_uri_handler(server_, &apiConfigGetUri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_, &apiConfigPostUri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_, &apiSendUri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_, &apiReceiveUri));

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

/*
 * GET /
 */
esp_err_t WebServer::rootGetHandler(httpd_req_t *req)
{
    return sendFile(
        req,
        "/spiffs/index.html",
        "text/html; charset=utf-8"
    );
}

/*
 * GET /style.css
 */
esp_err_t WebServer::styleGetHandler(httpd_req_t *req)
{
    return sendFile(
        req,
        "/spiffs/style.css",
        "text/css; charset=utf-8"
    );
}

/*
 * GET /app.js
 */
esp_err_t WebServer::appJsGetHandler(httpd_req_t *req)
{
    return sendFile(
        req,
        "/spiffs/app.js",
        "application/javascript; charset=utf-8"
    );
}

/*
 * GET /api/config
 *
 * 返回示例：
 * {
 *   "ssid": "LLCC68_Config",
 *   "freq": 470000000,
 *   "power": 22,
 *   "sf": 7,
 *   "bw": 125,
 *   "cr": 1,
 *   "sync": "0x0012",
 *   "mode": "tx"
 * }
 */
esp_err_t WebServer::apiConfigGetHandler(httpd_req_t *req)
{
    AppState& state = AppState::instance();
    LLCC68::Config cfg = state.getConfig();

    const char *mode = state.getLoraModeString();

    char buf[512];

    snprintf(
        buf,
        sizeof(buf),
        "{"
        "\"ssid\":\"%s\","
        "\"freq\":%lu,"
        "\"power\":%d,"
        "\"sf\":%u,"
        "\"bw\":%s,"
        "\"cr\":%u,"
        "\"sync\":\"%s\","
        "\"mode\":\"%s\""
        "}",
        WIFI_AP_SSID,
        static_cast<unsigned long>(cfg.freqHz),
        static_cast<int>(cfg.powerDbm),
        static_cast<unsigned>(cfg.spreadingFactor),
        bandwidthToString(cfg.bandwidth),
        static_cast<unsigned>(cfg.codingRate),
        toHex4(cfg.syncWord).c_str(),
        mode
    );

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    return httpd_resp_sendstr(req, buf);
}

/*
 * POST /api/config
 *
 * 请求：
 * {
 *   "freq": 470000000,
 *   "power": 22,
 *   "sf": 7,
 *   "bw": 125,
 *   "cr": 1,
 *   "sync": "0x12",
 *   "mode": "tx"
 * }
 */
esp_err_t WebServer::apiConfigPostHandler(httpd_req_t *req)
{
    std::string body;

    if (!readRequestBody(req, body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
        return ESP_FAIL;
    }

    AppState& state = AppState::instance();

    LLCC68::Config newCfg = state.getConfig();

    int intValue = 0;
    std::string strValue;

    if (jsonGetInt(body, "freq", intValue)) {
        if (intValue > 0) {
            newCfg.freqHz = static_cast<uint32_t>(intValue);
        }
    }

    if (jsonGetInt(body, "power", intValue)) {
        if (intValue >= -9 && intValue <= 22) {
            newCfg.powerDbm = static_cast<int8_t>(intValue);
        }
    }

    if (jsonGetInt(body, "sf", intValue)) {
        if (intValue >= 7 && intValue <= 12) {
            newCfg.spreadingFactor = static_cast<uint8_t>(intValue);
        }
    }

    if (jsonGetInt(body, "bw", intValue)) {
        char bwStr[16];
        snprintf(bwStr, sizeof(bwStr), "%d", intValue);
        newCfg.bandwidth = parseBandwidth(bwStr);
    }

    if (jsonGetInt(body, "cr", intValue)) {
        if (intValue >= 1 && intValue <= 4) {
            newCfg.codingRate = static_cast<uint8_t>(intValue);
        }
    }

    if (jsonGetString(body, "sync", strValue)) {
        newCfg.syncWord = static_cast<uint16_t>(
            strtoul(strValue.c_str(), nullptr, 0)
        );
    }

    if (jsonGetString(body, "mode", strValue)) {
        if (strValue == "tx") {
            state.setLoraModeTx();
        } else if (strValue == "rx") {
            state.setLoraModeRx();
        }
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

    httpd_resp_set_type(req, "application/json; charset=utf-8");

    return httpd_resp_sendstr(
        req,
        "{\"ok\":true}"
    );
}

/*
 * POST /api/send
 *
 * 请求：
 * {
 *   "message": "hello lora"
 * }
 */
esp_err_t WebServer::apiSendPostHandler(httpd_req_t *req)
{
    AppState& state = AppState::instance();

    if (!state.isLoraTxMode()) {
        httpd_resp_set_type(req, "application/json; charset=utf-8");
        return httpd_resp_sendstr(
            req,
            "{\"ok\":false,\"message\":\"LoRa 当前不是发送模式\"}"
        );
    }

    std::string body;

    if (!readRequestBody(req, body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
        return ESP_FAIL;
    }

    std::string msg;

    if (!jsonGetString(body, "message", msg)) {
        httpd_resp_set_type(req, "application/json; charset=utf-8");
        return httpd_resp_sendstr(
            req,
            "{\"ok\":false,\"message\":\"message 参数缺失\"}"
        );
    }

    size_t len = msg.length();

    if (len == 0 || len > 100) {
        httpd_resp_set_type(req, "application/json; charset=utf-8");
        return httpd_resp_sendstr(
            req,
            "{\"ok\":false,\"message\":\"消息长度必须为 1 到 100 个字符\"}"
        );
    }

    /*
     * 限制为 ASCII 英文字符。
     */
    for (size_t i = 0; i < len; ++i) {
        unsigned char ch = static_cast<unsigned char>(msg[i]);
        if (ch > 0x7F) {
            httpd_resp_set_type(req, "application/json; charset=utf-8");
            return httpd_resp_sendstr(
                req,
                "{\"ok\":false,\"message\":\"只能发送英文字符\"}"
            );
        }
    }

    ESP_LOGI(TAG, "LoRa send message: %s", msg.c_str());

    bool ok = state.enqueueTxMessage(msg.c_str());

    httpd_resp_set_type(req, "application/json; charset=utf-8");

    if (ok) {
        return httpd_resp_sendstr(req, "{\"ok\":true}");
    }

    return httpd_resp_sendstr(
        req,
        "{\"ok\":false,\"message\":\"发送队列已满\"}"
    );
}

/*
 * GET /api/receive
 *
 * 返回：
 * {
 *   "ok": true,
 *   "messages": []
 * }
 *
 * 没消息时返回空数组，前端不会刷新界面。
 */
esp_err_t WebServer::apiReceiveGetHandler(httpd_req_t *req)
{
    AppState& state = AppState::instance();

    if (!state.isLoraRxMode()) {
        httpd_resp_set_type(req, "application/json; charset=utf-8");
        return httpd_resp_sendstr(
            req,
            "{\"ok\":true,\"messages\":[]}"
        );
    }

    std::vector<AppState::RxMessage> messages = state.popRxMessages();

    std::string json;
    json.reserve(256 + messages.size() * 96);

    json += "{\"ok\":true,\"messages\":[";

    for (size_t i = 0; i < messages.size(); ++i) {
        const auto& m = messages[i];

        if (i > 0) {
            json += ",";
        }

        char meta[96];

        snprintf(
            meta,
            sizeof(meta),
            "\"rssi\":%d,\"snr\":%.2f",
            m.rssi,
            static_cast<double>(m.snr)
        );

        json += "{";
        json += "\"message\":\"";
        json += jsonEscape(m.message);
        json += "\",";
        json += meta;
        json += "}";
    }

    json += "]}";

    httpd_resp_set_type(req, "application/json; charset=utf-8");

    return httpd_resp_send(
        req,
        json.c_str(),
        json.length()
    );
}

esp_err_t WebServer::sendFile(
    httpd_req_t *req,
    const char *path,
    const char *contentType
)
{
    std::string content = readFile(path);

    if (content.empty()) {
        ESP_LOGE(TAG, "read file failed: %s", path);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, contentType);

    return httpd_resp_send(
        req,
        content.c_str(),
        content.length()
    );
}

bool WebServer::readRequestBody(
    httpd_req_t *req,
    std::string& out
)
{
    int totalLen = req->content_len;

    if (totalLen <= 0) {
        return false;
    }

    /*
     * 防止异常大包。
     * 你的页面目前最大请求体很小，给 1024 足够。
     */
    if (totalLen > 1024) {
        ESP_LOGW(TAG, "request body too large: %d", totalLen);
        return false;
    }

    out.clear();
    out.resize(totalLen);

    int received = 0;

    while (received < totalLen) {
        int ret = httpd_req_recv(
            req,
            &out[received],
            totalLen - received
        );

        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }

            ESP_LOGE(TAG, "httpd_req_recv failed: %d", ret);
            return false;
        }

        received += ret;
    }

    return true;
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
        ESP_LOGW(
            TAG,
            "file read incomplete: %u/%ld",
            static_cast<unsigned>(readLen),
            len
        );
    }

    return content;
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

bool WebServer::jsonGetString(
    const std::string& json,
    const char *key,
    std::string& out
)
{
    if (!key) {
        return false;
    }

    std::string pattern = "\"";
    pattern += key;
    pattern += "\"";

    size_t pos = json.find(pattern);
    if (pos == std::string::npos) {
        return false;
    }

    pos = json.find(':', pos + pattern.length());
    if (pos == std::string::npos) {
        return false;
    }

    pos++;

    while (pos < json.length() && isspace(static_cast<unsigned char>(json[pos]))) {
        pos++;
    }

    if (pos >= json.length() || json[pos] != '"') {
        return false;
    }

    pos++;

    std::string result;

    while (pos < json.length()) {
        char ch = json[pos++];

        if (ch == '"') {
            out = result;
            return true;
        }

        if (ch == '\\' && pos < json.length()) {
            char esc = json[pos++];

            switch (esc) {
                case '"':
                    result += '"';
                    break;

                case '\\':
                    result += '\\';
                    break;

                case '/':
                    result += '/';
                    break;

                case 'b':
                    result += '\b';
                    break;

                case 'f':
                    result += '\f';
                    break;

                case 'n':
                    result += '\n';
                    break;

                case 'r':
                    result += '\r';
                    break;

                case 't':
                    result += '\t';
                    break;

                default:
                    result += esc;
                    break;
            }
        } else {
            result += ch;
        }
    }

    return false;
}

bool WebServer::jsonGetInt(
    const std::string& json,
    const char *key,
    int& out
)
{
    if (!key) {
        return false;
    }

    std::string pattern = "\"";
    pattern += key;
    pattern += "\"";

    size_t pos = json.find(pattern);
    if (pos == std::string::npos) {
        return false;
    }

    pos = json.find(':', pos + pattern.length());
    if (pos == std::string::npos) {
        return false;
    }

    pos++;

    while (pos < json.length() && isspace(static_cast<unsigned char>(json[pos]))) {
        pos++;
    }

    if (pos >= json.length()) {
        return false;
    }

    char *endPtr = nullptr;

    long value = strtol(
        json.c_str() + pos,
        &endPtr,
        10
    );

    if (endPtr == json.c_str() + pos) {
        return false;
    }

    out = static_cast<int>(value);

    return true;
}

std::string WebServer::jsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.length() + 16);

    for (char ch : s) {
        switch (ch) {
            case '"':
                out += "\\\"";
                break;

            case '\\':
                out += "\\\\";
                break;

            case '\b':
                out += "\\b";
                break;

            case '\f':
                out += "\\f";
                break;

            case '\n':
                out += "\\n";
                break;

            case '\r':
                out += "\\r";
                break;

            case '\t':
                out += "\\t";
                break;

            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    char buf[8];
                    snprintf(
                        buf,
                        sizeof(buf),
                        "\\u%04X",
                        static_cast<unsigned char>(ch)
                    );
                    out += buf;
                } else {
                    out += ch;
                }
                break;
        }
    }

    return out;
}