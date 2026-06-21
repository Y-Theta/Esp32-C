#include "config_storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char* TAG = "ConfigStorage";
static const char* NVS_NS= "app";
static const char* NVS_KEY = "lora_cfg";

esp_err_t ConfigStorage::save(const LLCC68::Config& cfg)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_blob(h, NVS_KEY, &cfg, sizeof(cfg));
    if (err == ESP_OK) err = nvs_commit(h);

    nvs_close(h);
    ESP_LOGI(TAG, "config saved (%s)", esp_err_to_name(err));
    return err;
}

esp_err_t ConfigStorage::load(LLCC68::Config& cfg)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    size_t size = sizeof(cfg);
    err = nvs_get_blob(h, NVS_KEY, &cfg, &size);

    nvs_close(h);
    ESP_LOGI(TAG, "config loaded (%s)", esp_err_to_name(err));
    return err;
}