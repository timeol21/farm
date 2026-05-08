#pragma once
#include "data_layer/device/device_data_object.h"
#include "common/config/config_load.h"
#include <vector>
//从配置文件里面重新弄
class IDeviceRepository{
public:
    virtual ~IDeviceRepository() = default;

    virtual std::vector<HostNodeDefinition> loadHostNodes() = 0;

    virtual std::vector<InterfaceDefinition> loadInterfaces() = 0;

    virtual std::vector<DeviceDefinition> loadDevices() = 0;

    virtual bool buildRawModel()  = 0;

    virtual bool buildDomainModel() = 0;


    virtual void printRawModels() const = 0; 
};


class DeviceRepository : public IDeviceRepository{ 
public:
    DeviceRepository();
    
    ~DeviceRepository();


    std::vector<HostNodeDefinition> loadHostNodes() override;

    std::vector<InterfaceDefinition> loadInterfaces() override;

    std::vector<DeviceDefinition> loadDevices() override;

    bool buildRawModel() override;

    bool buildDomainModel() override;



    void printRawModels() const override;
    
private:
    // Raw层
    std::vector<RawHostNode> raw_hosts_;
    std::vector<RawInterface> raw_interfaces_;
    std::vector<RawDevice> raw_devices_;

    

    void fillMetadata(std::unordered_map<std::string, std::string>& map,const DeviceMetadata& meta);

    // Domain层
    std::vector<HostNodeDefinition> hosts_;
    std::vector<InterfaceDefinition> interfaces_;
    std::vector<DeviceDefinition> devices_;

    InterfaceType parseInterfaceType(const std::string& type);

    RuntimeBindingMode parseBindingMode(const RawDevice& r);

    DeviceRole parseDeviceRole(const RawDevice& r);

    bool hasChildren(const std::string& id);

    DeviceType parseDeviceType(const std::string& type);

    DeviceRole mapRoleFromType(DeviceType type, const RawDevice& r);
private:



};