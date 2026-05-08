#pragma once

#include "data_layer/device/vendor_sdk/vendor_sdk.h"
#include <memory>
#include <unordered_map>

class SdkEnvironmentManager {
public:
    bool initializeRequired(const std::set<VendorSdkType>& requiredTypes);
    
    void shutdownAll();

    bool isAvailable(VendorSdkType type) const;
    std::shared_ptr<ISdkEnvironment> get(VendorSdkType type) const;

private:
    std::shared_ptr<ISdkEnvironment> createEnvironment(VendorSdkType type);

private:
    std::unordered_map<VendorSdkType, std::shared_ptr<ISdkEnvironment>> envs_;
};

