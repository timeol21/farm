#pragma once

#include "data_layer/device/runtime/factory.h"
#include "data_layer/device/repository.h"

#include "business_layer/buffer/frame_buffer.h"

class IDeviceRuntimeBuilder{
public:
    virtual ~IDeviceRuntimeBuilder() = default;

    virtual std::unordered_map<std::string, std::shared_ptr<DeviceRuntime>>  buildRuntime() = 0;
    virtual bool prepareEnvironment(const std::vector<DeviceDefinition>& definitions) = 0;

};

class DeviceRuntimeBuilder : public IDeviceRuntimeBuilder{

public:
     DeviceRuntimeBuilder(IDeviceRepository& repository,
                         DeviceRuntimeFactory& runtimeFactory,
                         DeviceAccessPathResolver& driverAccessPathResolver,
                         DriverFactory& driverFactory,
                         DeviceTopologyBuilder& topologyBuilder,
                         VendorRequirementAnalyzer& vendorRequirementAnalyzer,
                         SdkEnvironmentManager& sdkManager,
                         std::shared_ptr<FrameBuffer> frameBuffer);

    ~DeviceRuntimeBuilder() override;

    std::unordered_map<std::string, std::shared_ptr<DeviceRuntime>>  buildRuntime() override;
    
    bool prepareEnvironment(const std::vector<DeviceDefinition>& definitions) override;

private:
    bool RegisterFrameCamera(std::vector<DeviceDefinition> devices,std::shared_ptr<RuntimeContext> context_);



private:
    IDeviceRepository& repository_; //这个给相应的设备的配置信息

    DeviceRuntimeFactory& runtimeFactory_; //设备运行时工厂

    DeviceAccessPathResolver& driverAccessPathResolver_; // 驱动解析器
    
    DriverFactory& driverFactory_; // 驱动工厂

    DeviceTopologyBuilder& topologyBuilder_; //设备拓扑构建器

    VendorRequirementAnalyzer& vendorRequirementAnalyzer_; //供应商需求分析器

    SdkEnvironmentManager& sdkManager_; // SDK环境管理器  
    
    std::shared_ptr<RuntimeContext> context_;//设备运行时里面的内容
};

