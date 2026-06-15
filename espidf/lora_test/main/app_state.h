#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "llcc68.h"

class AppState {
public:
    static AppState& instance();

    bool init();

    LLCC68::Config getConfig();
    void setConfig(const LLCC68::Config& cfg);

    void setNeedApply(bool need);
    bool takeNeedApply();

    SemaphoreHandle_t configMutex() const;
    SemaphoreHandle_t radioMutex() const;

private:
    AppState() = default;
    AppState(const AppState&) = delete;
    AppState& operator=(const AppState&) = delete;

private:
    LLCC68::Config config_{};

    SemaphoreHandle_t configMutex_ = nullptr;
    SemaphoreHandle_t radioMutex_ = nullptr;

    volatile bool needApplyConfig_ = false;
};