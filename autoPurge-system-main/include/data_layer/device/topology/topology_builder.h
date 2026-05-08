#pragma once
#include "data_layer/device/topology/device_topology.h"
#include <unordered_set>

class DeviceTopologyBuilder{
public:
    DeviceTopology build(
        const std::vector<HostNodeDefinition>& hostNodeDefinitions,
        const std::vector<InterfaceDefinition>& interfaceDefinitions,
        const std::vector<DeviceDefinition>& deviceDefinitions
    );
    
private:
    void validateHosts(const std::vector<HostNodeDefinition>& hostNodeDef);

    void validateInterfaces(
        const std::vector<InterfaceDefinition>& interfaceDef,
        const std::unordered_map<std::string,std::shared_ptr<HostNodeDefinition>>& hostNodes
    );

    void validateDevices(
        const std::vector<DeviceDefinition>& deviceDef,
        const std::unordered_map<std::string,std::shared_ptr<HostNodeDefinition>>& hostNodes
    );

    void buildParentChildRelations(std::unordered_map<std::string,std::shared_ptr<DeviceNode>>& deviceNodes);
};

