#include "camera/UnitCamS3_5MP.h"

void UnitCamS3_5MP::Start() {

    if (!_config.wifiSsid.empty()) {
        StartForWorking();
    }else{
        StartForSetting();
    }

    _config.wifiSsid = "";
    SaveConfig();
}

void UnitCamS3_5MP::StartForWorking() {
}
