#include "camera/UnitCamS3_5MP.h"
#include "services/SettingService.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"

static const char* TAG = "PY260 Transfer";

UnitCamS3_5MP *camera = nullptr;

namespace Operation {

static const char *boundary = "-------562164BDF";
static char constpostinfo[512];
static char footer[32];

static void init_post_data() {
    snprintf(constpostinfo, sizeof(constpostinfo), 
             "--%s\r\n"
             "Content-Disposition: form-data; name=\"image\"; filename=\"photo.jpeg\"\r\n"
             "Content-Type: image/jpeg\r\n\r\n", boundary);
    snprintf(footer, sizeof(footer), "\r\n--%s--\r\n", boundary);
}

static esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            // ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            // ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        default:
            break;
    }
    return ESP_OK;
}

static void upload_photo(camera_fb_t *fb, UnitCamS3_5MP *ump) {
    SettingService& settings = SettingService::getInstance();
    const char *server = settings.getConfig().postServer.c_str();
    int port = settings.getConfig().postPort;

    char url[256];
    snprintf(url, sizeof(url), "https://%s:%d/imgup", server, port);

    esp_http_client_config_t config = {};
    config.url = url;
    config.event_handler = _http_event_handler;
    config.timeout_ms = 10000;
    config.transport_type = HTTP_TRANSPORT_OVER_SSL;
    config.skip_cert_common_name_check = true;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGI(TAG, "Failed to init HTTP client");
        return;
    }

    int contentLength = strlen(constpostinfo) + fb->len + strlen(footer);

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Accept", "*/*");
    esp_http_client_set_header(client, "Connection", "Keep-Alive");
    
    char content_type[64];
    snprintf(content_type, sizeof(content_type), "multipart/form-data; boundary=%s", boundary);
    esp_http_client_set_header(client, "Content-Type", content_type);
    
    char content_length_hdr[32];
    snprintf(content_length_hdr, sizeof(content_length_hdr), "%d", contentLength);
    esp_http_client_set_header(client, "Content-Length", content_length_hdr);

    esp_http_client_open(client, contentLength);

    int written = 0;
    written += esp_http_client_write(client, constpostinfo, strlen(constpostinfo));
    written += esp_http_client_write(client, (const char *)fb->buf, fb->len);
    written += esp_http_client_write(client, footer, strlen(footer));
    ESP_LOGI(TAG, "Total bytes written: %d / %d", written, contentLength);

    int fetch_ret = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    int resp_content_length = esp_http_client_get_content_length(client);
    ESP_LOGI(TAG, "fetch_headers ret=%d, status=%d, content_length=%d", fetch_ret, status_code, resp_content_length);
    
    if (status_code == 200) {
        ESP_LOGI(TAG, "Upload successful");
    } else {
        ESP_LOGI(TAG, "Upload failed, status code: %d", status_code);
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}

} // namespace Operation

extern "C" void app_main(void) {
    vTaskDelay(3000 / portTICK_PERIOD_MS);

    Operation::init_post_data();

    camera = new UnitCamS3_5MP();
    camera->OnProcessImage = Operation::upload_photo;
    camera->Init();
    camera->StartForWorking();
}