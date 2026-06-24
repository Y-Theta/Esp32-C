#pragma once

#include <string>
#include "common.h"

class StorageService {
public:
    static StorageService& getInstance() {
        static StorageService instance;
        return instance;
    }

    StorageService(const StorageService&) = delete;
    StorageService& operator=(const StorageService&) = delete;

    void init();
    void load();
    void save();
    
    // 获取配置
    const CONFIG::SystemConfig_t& getConfig() const;
    
    // 更新配置
    void setConfig(const CONFIG::SystemConfig_t& config);
    
    // 更新特定配置
    void setWifiConfig(const std::string& ssid, const std::string& password);
    void setCameraConfig(int jpegQuality, int frameSize);
    void setPostConfig(const std::string& server, int port, int interval);

private:
    StorageService() = default;
    ~StorageService() = default;
    
    bool initSpiffs();
    CONFIG::SystemConfig_t _config;
    bool _initialized = false;
    bool _spiffsInitialized = false;
};