#pragma once
#include <vector>
#include <set>
#include "data_layer/device/device_data_object.h"
extern "C" {
    #include <libavformat/avformat.h>
    // #include <libavcodec/avcodec.h>
    // #include <libswscale/swscale.h>
}


class VendorRequirementAnalyzer {
public:
    VendorRequirementAnalyzer();
    ~VendorRequirementAnalyzer();

    std::set<VendorSdkType> analyze(const std::vector<DeviceDefinition>& devices) const;

private:
    VendorSdkType resolveVendorSdk(const DeviceDefinition& device) const;
};

class ISdkEnvironment{
public:
    virtual ~ISdkEnvironment() = default;

    virtual VendorSdkType type() const = 0;

    virtual bool initialize() = 0;

    virtual void shutdown() = 0;

    virtual bool isInitialized() const = 0;
};

class HikvisionSdkEnvironment : public ISdkEnvironment {
public:
    HikvisionSdkEnvironment();
    ~HikvisionSdkEnvironment();

    VendorSdkType type() const override {
        return VendorSdkType::Hikvision;
    }

    bool initialize() override;
    void shutdown() override;
    bool isInitialized() const override {
        return initialized_;
    }

private:
    bool initialized_ = false;
};


class FFmpegEnvironment : public ISdkEnvironment {
public:
    FFmpegEnvironment();
    ~FFmpegEnvironment();

    VendorSdkType type() const override ;

    bool initialize() override;
    
    void shutdown() override;

    bool isInitialized() const override ;

private:
    bool initialized_ = false;
};