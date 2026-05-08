#pragma once
#include "business_layer/device/device_runtime_manager.h"
#include <optional>
class IDeviceStateQuery{
public:
    virtual ~IDeviceStateQuery() = default;

    virtual std::optional<DeviceRuntimeStatus> queryRuntimeStatus(const std::string& deviceId) const = 0;
    
    virtual DeviceStatusView queryDeviceStatusView(const std::string& deviceId) const = 0;


};

class DeviceStateQuery : public IDeviceStateQuery{
public:
    explicit DeviceStateQuery(std::shared_ptr<DeviceStatusCache> statusCache);
    ~DeviceStateQuery() override;

    std::optional<DeviceRuntimeStatus> queryRuntimeStatus(const std::string& deviceId) const override;

    DeviceStatusView queryDeviceStatusView(const std::string& deviceId) const override;

private:
    std::shared_ptr<DeviceStatusCache> statusCache_;
};