#include "data_layer/device/device_runtime_builder.h"
#include "data_layer/device/vendor_sdk/sdk_environment_manager.h"
#include "data_layer/device/topology/access_path.h"
#include "data_layer/device/topology/topology_builder.h"
#include "include/common/log/log_manager.h"
#include <exception>

DeviceRuntimeBuilder::DeviceRuntimeBuilder(
    IDeviceRepository& repository,
    DeviceRuntimeFactory& runtimeFactory,
    DeviceAccessPathResolver& driverAccessPathResolver,
    DriverFactory& driverFactory,
    DeviceTopologyBuilder& topologyBuilder,
    VendorRequirementAnalyzer& vendorRequirementAnalyzer,
    SdkEnvironmentManager& sdkManager,
    std::shared_ptr<FrameBuffer> frameBuffer
)  : repository_(repository),
      runtimeFactory_(runtimeFactory),
      driverAccessPathResolver_(driverAccessPathResolver),
      driverFactory_(driverFactory),
      topologyBuilder_(topologyBuilder),
      vendorRequirementAnalyzer_(vendorRequirementAnalyzer),
      sdkManager_(sdkManager)

{   
    context_ = std::make_shared<RuntimeContext>();
    context_->frameBuffer = frameBuffer;
}

DeviceRuntimeBuilder::~DeviceRuntimeBuilder() {
    
}

std::unordered_map<std::string, std::shared_ptr<DeviceRuntime>> DeviceRuntimeBuilder::buildRuntime() {
    std::unordered_map<std::string, std::shared_ptr<DeviceRuntime>> runtimes;

    // 构建原始设备模型
    if(!repository_.buildRawModel()){
        LOG_ERROR("DeviceRuntimeBuilder：原始设备模型构建失败");
        return runtimes;
    }
    LOG_INFO("DeviceRuntimeBuilder：原始设备模型构建成功");

    // 构建领域设备模型
    try {
        repository_.buildDomainModel();
    } catch (const std::exception& e) {
        // ✅ 正确拼接 string，不再用 + 连接常量
        std::string err = "DeviceRuntimeBuilder：领域设备模型构建失败，异常信息：" + std::string(e.what());
        LOG_ERROR(err);
        return runtimes;
    }
    
    LOG_INFO("DeviceRuntimeBuilder：领域设备模型构建成功"); 

    //主设备    
    auto hostNode = repository_.loadHostNodes();
    //串口接口
    auto interfaces = repository_.loadInterfaces();
    //设备单体
    auto devices = repository_.loadDevices();

    //加载设备的所需要的环境
    auto requireSdks = vendorRequirementAnalyzer_.analyze(devices);

    if(!sdkManager_.initializeRequired(requireSdks)){
        LOG_ERROR("DeviceRuntimeBuilder：设备依赖 SDK 环境初始化失败");
        return runtimes;
    }

    if(!RegisterFrameCamera(devices, context_)){
        LOG_ERROR("DeviceRuntimeBuilder：帧缓冲摄像头通道注册失败");
        return runtimes;
    }

    // 拓扑创建
    DeviceTopology topology;
    try {
        topology = topologyBuilder_.build(hostNode, interfaces, devices);
    } 
    catch (const std::exception& e) { // ✅ 扩大捕获范围，防止崩溃
        std::string errMsg = "DeviceRuntimeBuilder：设备拓扑关系构建失败，异常信息：" + std::string(e.what());
        LOG_ERROR(errMsg);
        return runtimes;
    }

    // 检查拓扑是否为空
    if (topology.deviceNodes.empty()) {
        LOG_ERROR("DeviceRuntimeBuilder：设备拓扑节点为空，无法创建设备运行时");
        return runtimes;
    }

    // 遍历拓扑创建设备运行时
    for(const auto& [deviceId, node] : topology.deviceNodes){
        auto driver = driverFactory_.createDeviceDriver(*node, sdkManager_);

        std::shared_ptr<DeviceRuntime> runtime;
        try {
            runtime = runtimeFactory_.createRuntime(node, driver, context_);
        }catch (const std::exception& e) { // ✅ 统一捕获所有标准异常
            std::string err = "DeviceRuntimeBuilder：设备" + deviceId + "运行时创建失败，异常信息：" + e.what();
            LOG_ERROR(err);
        }

        if (!runtime) {
           
            std::string err = "DeviceRuntimeBuilder：设备" + deviceId + "运行时创建失败";
            LOG_ERROR(err);
            continue;
        }

        runtimes[deviceId] = runtime;
    }

    std::string str = std::to_string(runtimes.size());
    std::string logInfo = "DeviceRuntimeBuilder：共成功创建" + str + "个设备运行时";
    LOG_INFO(logInfo);
    
    return runtimes;
}


bool DeviceRuntimeBuilder::RegisterFrameCamera(std::vector<DeviceDefinition> devices,std::shared_ptr<RuntimeContext> context_)
{

    if (!context_) {
        return false;
    }
    bool allSuccess = true;
    // 遍历所有设备
    for (const auto& dev : devices) {
        // 只处理摄像头类型设备
        if (dev.deviceTypeEnum != DeviceType::CAMERA) {
            continue;
        }
        CameraChannelInfo info{};
        info.deviceId = dev.id;
        info.relevanceId = dev.parentDeviceId;
        bool ok = context_->frameBuffer->registerCamera(info);
        if (!ok) {
            allSuccess = false;
        }
    }
    return allSuccess;
}


bool DeviceRuntimeBuilder::prepareEnvironment(const std::vector<DeviceDefinition>& definitions) {
    return true;
}


DeviceAccessPath DeviceAccessPathResolver::resolve(const std::shared_ptr<DeviceNode>& node) const{
    DeviceAccessPath path;

    // 1. 空节点校验
    if (!node) {
        path.fullPath = "/invalid/empty_node";
        return path;
    }

    const auto& devDef = node->definition;
    path.deviceId = devDef.id;
    path.deviceAddress = devDef.address;

    // 2. 解析父设备路径（构建层级结构）
    path.parentPath = resolveParentPath(node->parent);

    // 3. 宿主节点信息（必须）
    if (node->host) {
        path.hostId = node->host->id;
        path.hostIp = node->host->ip;
    } else {
        path.hostId = "unknown_host";
        path.hostIp = "0.0.0.0";
    }

    // 4. 接口信息（必须）
    if (node->interface) {
        path.interfaceType = interfaceTypeToString(node->interface->type);
        path.interfaceEndpoint = node->interface->endpoint;
    } else {
        path.interfaceType = "unknown_interface";
        path.interfaceEndpoint = "unknown_endpoint";
    }

    // 5. 自动拼接完整唯一路径
    path.buildFullPath();

    return path;
}

// 辅助：递归获取父设备路径
std::string DeviceAccessPathResolver::resolveParentPath(const std::weak_ptr<DeviceNode>& parentNode) const {
    auto parent = parentNode.lock();
    if (!parent) {
        return ""; // 根节点无父路径
    }
    // 递归解析父节点，返回父节点的完整路径
    return resolve(parent).fullPath;
}

// 辅助：接口类型枚举转字符串
std::string DeviceAccessPathResolver::interfaceTypeToString(InterfaceType type) const {
    switch (type) {
        case InterfaceType::USB: return "usb";
        case InterfaceType::RS485: return "rs485";
        // case InterfaceType::ETHERNET: return "eth";
        case InterfaceType::SDK: return "sdk";
        default: return "unknown";
    }
}

DeviceTopology DeviceTopologyBuilder::build(
        const std::vector<HostNodeDefinition>& hostNodeDefinitions,
        const std::vector<InterfaceDefinition>& interfaceDefinitions,
        const std::vector<DeviceDefinition>& deviceDefinitions
){
        DeviceTopology topology;
        // 步骤1：校验宿主节点合法性
        validateHosts(hostNodeDefinitions);
        // 将宿主节点存入map（id为key）
        for (const auto& host : hostNodeDefinitions) {
            topology.hostNodes[host.id] = std::make_shared<HostNodeDefinition>(host);
        }
        // 步骤2：校验接口合法性（依赖宿主节点）
        validateInterfaces(interfaceDefinitions, topology.hostNodes);
        // 将接口存入map（id为key）
        for (const auto& iface : interfaceDefinitions) {
            topology.interfaceNodes[iface.id] = std::make_shared<InterfaceDefinition>(iface);
        }
        // 步骤3：校验设备合法性（依赖宿主节点）
        validateDevices(deviceDefinitions, topology.hostNodes);
        // 步骤4：构建设备节点，关联宿主、接口
        for (const auto& devDef : deviceDefinitions) {
            auto deviceNode = std::make_shared<DeviceNode>();
            // 基础属性赋值
            deviceNode->definition = devDef;
            // 关联宿主节点（已校验过，一定存在）
            deviceNode->host = topology.hostNodes.at(devDef.hostNodeId);
            // 关联接口（接口ID非空时才关联）
            if (!devDef.interfaceId.empty()) {
                deviceNode->interface = topology.interfaceNodes.at(devDef.interfaceId);
            }

            topology.deviceNodes[devDef.id] = deviceNode;
        }
        // 步骤5：构建设备父子关系
        buildParentChildRelations(topology.deviceNodes);

    return topology;
}


void DeviceTopologyBuilder::validateHosts(const std::vector<HostNodeDefinition>& hostNodeDef){
        std::unordered_set<std::string> ifaceIds;
        for (const auto& iface : hostNodeDef) {
            // 接口ID校验
            if (iface.id.empty()) {
                throw std::invalid_argument(std::string("接口ID不能为空"));
            }
            
            if (ifaceIds.count(iface.id)) {
                throw std::invalid_argument(std::string("接口ID重复: " + iface.id));
            }

            ifaceIds.insert(iface.id);

            // 所属宿主节点必须存在
            if (iface.id.empty() || !ifaceIds.count(iface.id)) {
                throw std::invalid_argument(std::string("接口[" + iface.id + "]关联的宿主节点不存在: " + iface.id));
            }
        }
}

void DeviceTopologyBuilder::validateInterfaces(
        const std::vector<InterfaceDefinition>& interfaceDef,
        const std::unordered_map<std::string,std::shared_ptr<HostNodeDefinition>>& hostNodes
){
      std::unordered_set<std::string> ifaceIds;
        for (const auto& iface : interfaceDef) {
            // 接口ID校验
            if (iface.id.empty()) {
                throw std::invalid_argument(std::string("接口ID不能为空"));
            }
            if (ifaceIds.count(iface.id)) {
                throw std::invalid_argument(std::string("接口ID重复: " + iface.id));
            }
            ifaceIds.insert(iface.id);

            // 所属宿主节点必须存在
            if (iface.hostNodeId.empty() || !hostNodes.count(iface.hostNodeId)) {
                throw std::invalid_argument(std::string("接口[" + iface.id + "]关联的宿主节点不存在: " + iface.hostNodeId));
            }
        }
}

void DeviceTopologyBuilder::validateDevices(
        const std::vector<DeviceDefinition>& deviceDef,
        const std::unordered_map<std::string,std::shared_ptr<HostNodeDefinition>>& hostNodes
){
        std::unordered_set<std::string> deviceIds;
        std::unordered_set<std::string> parentDeviceIds;

        // 第一次遍历：基础校验 + 收集父设备ID
        for (const auto& dev : deviceDef) {
            // 设备ID校验
            if (dev.id.empty()) {
                throw std::invalid_argument(std::string("设备ID不能为空"));
            }
            if (deviceIds.count(dev.id)) {
                throw std::invalid_argument(std::string("设备ID重复: " + dev.id));
            }
            deviceIds.insert(dev.id);

            // 所属宿主节点必须存在
            if (dev.hostNodeId.empty() || !hostNodes.count(dev.hostNodeId)) {
                throw std::invalid_argument(std::string("设备[" + dev.id + "]关联的宿主节点不存在: " + dev.hostNodeId));
            }

            // 绑定模式校验：Dependent必须指定父设备，Independent不能指定父设备
            if (dev.bindingMode == RuntimeBindingMode::Dependent) {
                if (dev.parentDeviceId.empty()) {
                    throw std::invalid_argument(std::string("设备[" + dev.id + "]为依附模式，必须指定父设备ID"));
                }
                parentDeviceIds.insert(dev.parentDeviceId);
            } else {
                if (!dev.parentDeviceId.empty()) {
                    throw std::invalid_argument(std::string("设备[" + dev.id + "]为独立模式，不能指定父设备ID"));
                }
            }
        }

        // 第二次遍历：校验父设备是否存在
        for (const auto& dev : deviceDef) {
            if (dev.bindingMode == RuntimeBindingMode::Dependent && !deviceIds.count(dev.parentDeviceId)) {
                throw std::invalid_argument(std::string("设备[" + dev.id + "]的父设备ID不存在: " + dev.parentDeviceId));
            }
        }  
}

void DeviceTopologyBuilder::buildParentChildRelations(std::unordered_map<std::string,std::shared_ptr<DeviceNode>>& deviceNodes){
        // 清空所有父子关系
        for (auto& pair : deviceNodes) {
            auto& node = pair.second;
            node->parent;
            node->children.clear();
        }

        // 建立关联
        for (auto& pair : deviceNodes) {
            auto& childNode = pair.second;
            const auto& parentId = childNode->definition.parentDeviceId;

            if (!parentId.empty() && deviceNodes.count(parentId)) {
                auto parentNode = deviceNodes.at(parentId);
                childNode->parent = parentNode;
                parentNode->children.push_back(childNode);
            }
        }
}


bool SdkEnvironmentManager::initializeRequired(const std::set<VendorSdkType>& requiredTypes){
        for(auto type : requiredTypes){
                if(envs_.count(type)){
                      continue;  
                }

                auto env = createEnvironment(type);
                if(!env){
                     return false;   
                }

                if(!env->initialize()){
                    return false;
                }

                envs_[type] = env;

        }
        return true;
}

void SdkEnvironmentManager::shutdownAll(){
        for (auto& [type, env] : envs_) {
                if (env) {
                env->shutdown();
                }
        }

    envs_.clear();

}

bool SdkEnvironmentManager::isAvailable(VendorSdkType type) const{
        auto it = envs_.find(type);
        if (it == envs_.end()) return false;

        return it->second->isInitialized();
}

std::shared_ptr<ISdkEnvironment> SdkEnvironmentManager::get(VendorSdkType type) const{

        auto it = envs_.find(type);
        if (it != envs_.end()) {
                return it->second;
        }
        return nullptr;
}


std::shared_ptr<ISdkEnvironment> SdkEnvironmentManager::createEnvironment(VendorSdkType type){

        switch (type) {
        case VendorSdkType::Hikvision:
            return std::make_shared<HikvisionSdkEnvironment>();

        case VendorSdkType::RTSP:
            return std::make_shared<FFmpegEnvironment>();

        }        
        return nullptr;
}

