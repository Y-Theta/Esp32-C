#pragma once
#include "llcc68.h"

namespace ConfigStorage {
    esp_err_t save(const LLCC68::Config& cfg);
    esp_err_t load(LLCC68::Config& cfg);       // 返回 ESP_ERR_NVS_NOT_FOUND 时用默认值
}