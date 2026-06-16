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
    loraMode_ = LoraMode::TX;

    txQueue_.clear();
    rxQueue_.clear();

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

/*
 * LoRa 模式
 */
void AppState::setLoraModeTx()
{
    xSemaphoreTake(configMutex_, portMAX_DELAY);
    loraMode_ = LoraMode::TX;
    xSemaphoreGive(configMutex_);
}

void AppState::setLoraModeRx()
{
    xSemaphoreTake(configMutex_, portMAX_DELAY);
    loraMode_ = LoraMode::RX;
    xSemaphoreGive(configMutex_);
}

bool AppState::isLoraTxMode()
{
    xSemaphoreTake(configMutex_, portMAX_DELAY);
    bool ret = (loraMode_ == LoraMode::TX);
    xSemaphoreGive(configMutex_);
    return ret;
}

bool AppState::isLoraRxMode()
{
    xSemaphoreTake(configMutex_, portMAX_DELAY);
    bool ret = (loraMode_ == LoraMode::RX);
    xSemaphoreGive(configMutex_);
    return ret;
}

const char *AppState::getLoraModeString()
{
    xSemaphoreTake(configMutex_, portMAX_DELAY);
    const char *mode = (loraMode_ == LoraMode::TX) ? "tx" : "rx";
    xSemaphoreGive(configMutex_);
    return mode;
}

/*
 * 发送队列
 */
bool AppState::enqueueTxMessage(const char *msg)
{
    if (!msg) {
        return false;
    }

    xSemaphoreTake(configMutex_, portMAX_DELAY);

    /*
     * 队列长度限制，防止内存无限增长
     */
    if (txQueue_.size() >= 8) {
        xSemaphoreGive(configMutex_);
        return false;
    }

    txQueue_.push_back(std::string(msg));

    xSemaphoreGive(configMutex_);
    return true;
}

bool AppState::popTxMessage(std::string& msg)
{
    xSemaphoreTake(configMutex_, portMAX_DELAY);

    if (txQueue_.empty()) {
        xSemaphoreGive(configMutex_);
        return false;
    }

    msg = txQueue_.front();
    txQueue_.erase(txQueue_.begin());

    xSemaphoreGive(configMutex_);
    return true;
}

/*
 * 接收缓存
 */
void AppState::pushRxMessage(const std::string& msg, int rssi, float snr)
{
    xSemaphoreTake(configMutex_, portMAX_DELAY);

    /*
     * 接收缓存也限制长度，避免一直堆积
     */
    if (rxQueue_.size() >= 32) {
        rxQueue_.erase(rxQueue_.begin());
    }

    RxMessage item;
    item.message = msg;
    item.rssi = rssi;
    item.snr = snr;

    rxQueue_.push_back(item);

    xSemaphoreGive(configMutex_);
}

std::vector<AppState::RxMessage> AppState::popRxMessages()
{
    xSemaphoreTake(configMutex_, portMAX_DELAY);

    std::vector<RxMessage> out = rxQueue_;
    rxQueue_.clear();

    xSemaphoreGive(configMutex_);

    return out;
}