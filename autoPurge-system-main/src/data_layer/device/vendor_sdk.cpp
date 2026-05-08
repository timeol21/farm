#include "data_layer/device/vendor_sdk/vendor_sdk.h"
#include "common/log/log_manager.h"
VendorRequirementAnalyzer::VendorRequirementAnalyzer(){

}

VendorRequirementAnalyzer::~VendorRequirementAnalyzer(){
    
}


std::set<VendorSdkType> VendorRequirementAnalyzer::analyze(const std::vector<DeviceDefinition>& devices) const{
    std::set<VendorSdkType> result;
    for (const auto& device : devices) {
        if (!device.enabled) continue;

        auto sdk = resolveVendorSdk(device);
        if (sdk != VendorSdkType::Unknown) {
            result.insert(sdk);
        }
    }

    return result;
}


VendorSdkType VendorRequirementAnalyzer::resolveVendorSdk(const DeviceDefinition& device) const{
    const auto& vendor = device.vendor;

    if (vendor == "hikvision") return VendorSdkType::Hikvision;
    if (vendor == "rtsp")     return VendorSdkType::RTSP;
    if (vendor == "dahua")    return VendorSdkType::Dahua;
    if (vendor == "radar_a")  return VendorSdkType::RadarA;
    if (vendor == "radar_b")  return VendorSdkType::RadarB;
    if (vendor == "plc_a")    return VendorSdkType::PlcVendorA;
    if (vendor == "plc_b")    return VendorSdkType::PlcVendorB;
    return VendorSdkType::Unknown;
}



HikvisionSdkEnvironment::HikvisionSdkEnvironment(){

}

HikvisionSdkEnvironment::~HikvisionSdkEnvironment(){

}

bool HikvisionSdkEnvironment::initialize() {
        return true;
}


void HikvisionSdkEnvironment::shutdown() {
    
}


FFmpegEnvironment::FFmpegEnvironment(){

}

FFmpegEnvironment::~FFmpegEnvironment(){
    shutdown();
}

VendorSdkType FFmpegEnvironment::type() const  {
    return VendorSdkType::RTSP;
}

bool FFmpegEnvironment::initialize() {
    if (initialized_) return true;
    avformat_network_init();  // ⚡ 全局初始化 FFmpeg 网络模块
    initialized_ = true;
    return true;
}

void FFmpegEnvironment::shutdown() {
    if (initialized_) {
        avformat_network_deinit();  // ⚡ 全局反初始化
        initialized_ = false;
    }
}
bool FFmpegEnvironment::isInitialized() const  {
        return initialized_;
}
