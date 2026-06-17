#pragma once

#include <string>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "llcc68.h"

class AppState {
public:
    enum class LoraMode {
        TX,
        RX
    };

    struct RxMessage {
        std::string message;
        int rssi;
        float snr;
    };

public:
    static AppState& instance();

    bool init();

    LLCC68::Config getConfig();
    void setConfig(const LLCC68::Config& cfg);

    void setNeedApply(bool need);
    bool takeNeedApply();

    SemaphoreHandle_t configMutex() const;
    SemaphoreHandle_t radioMutex() const;

    /*
     * LoRa 模式
     */
    void setLoraModeTx();
    void setLoraModeRx();
    bool isLoraTxMode();
    bool isLoraRxMode();
    const char *getLoraModeString();

    /*
     * 发送队列
     */
    bool enqueueTxMessage(const char *msg);
    bool popTxMessage(std::string& msg);

    /*
     * 接收缓存
     */
    void pushRxMessage(const std::string& msg, int rssi, float snr);
    std::vector<RxMessage> popRxMessages();

private:
    AppState() = default;
    AppState(const AppState&) = delete;
    AppState& operator=(const AppState&) = delete;

private:
    LLCC68::Config config_{};

    SemaphoreHandle_t configMutex_ = nullptr;
    SemaphoreHandle_t radioMutex_ = nullptr;

    volatile bool needApplyConfig_ = false;

    LoraMode loraMode_ = LoraMode::TX;

    std::vector<std::string> txQueue_;
    std::vector<RxMessage> rxQueue_;
};