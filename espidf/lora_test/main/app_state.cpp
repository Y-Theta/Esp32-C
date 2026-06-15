#include "app_state.h"

#include "esp_log.h"

static const char *TAG = "AppState";

AppState& AppState::instance()
{
    static AppState state;
    return state;
}

bool AppState::init()
{
    if (!configMutex_) {
        configMutex_ = xSemaphoreCreateMutex();
    }

    if (!radioMutex_) {
        radioMutex_ = xSemaphoreCreateMutex();
    }

    if (!configMutex_ || !radioMutex_) {
        ESP_LOGE(TAG, "create mutex failed");
        return false;
    }

    /*
     * 默认 LoRa 配置
     * 保持你原来的配置不变
     */
    config_.freqHz = 433000000;
    config_.powerDbm = 14;
    config_.spreadingFactor = 9;
    config_.bandwidth = 0x04;
    config_.codingRate = 1;
    config_.syncWord = 0x1424;

    needApplyConfig_ = false;

    return true;
}

LLCC68::Config AppState::getConfig()
{
    LLCC68::Config cfg{};

    xSemaphoreTake(configMutex_, portMAX_DELAY);
    cfg = config_;
    xSemaphoreGive(configMutex_);

    return cfg;
}

void AppState::setConfig(const LLCC68::Config& cfg)
{
    xSemaphoreTake(configMutex_, portMAX_DELAY);
    config_ = cfg;
    xSemaphoreGive(configMutex_);
}

void AppState::setNeedApply(bool need)
{
    xSemaphoreTake(configMutex_, portMAX_DELAY);
    needApplyConfig_ = need;
    xSemaphoreGive(configMutex_);
}

bool AppState::takeNeedApply()
{
    bool need = false;

    xSemaphoreTake(configMutex_, portMAX_DELAY);

    need = needApplyConfig_;
    if (needApplyConfig_) {
        needApplyConfig_ = false;
    }

    xSemaphoreGive(configMutex_);

    return need;
}

SemaphoreHandle_t AppState::configMutex() const
{
    return configMutex_;
}

SemaphoreHandle_t AppState::radioMutex() const
{
    return radioMutex_;
}