#include "business_layer/device/device_state_query.h"

DeviceStateQuery::DeviceStateQuery(std::shared_ptr<DeviceStatusCache> statusCache){

}

DeviceStateQuery::~DeviceStateQuery() {

}

std::optional<DeviceRuntimeStatus> DeviceStateQuery::queryRuntimeStatus(const std::string& deviceId) const {
    return std::nullopt;
}

DeviceStatusView DeviceStateQuery::queryDeviceStatusView(const std::string& deviceId) const {
    return DeviceStatusView();
}