#pragma once

#include "business_layer/device/device_service_object.h"
#include "data_layer/device/device_runtime_builder.h"
#include <optional>
#include <thread>

//控制，维持整个设备的运行状态，从出生到结束，从出生到异常到恢复到结束

class IDeviceRuntimeManager{
public:
    virtual ~IDeviceRuntimeManager() = default;

    virtual bool initializeAll() = 0;//把配置文件里面的设备变成一个个的runtime device
    virtual bool startAll() = 0;//开启所有设备
    virtual void shutdownAll() = 0;//关闭所有设备


    virtual bool initializeDevice(const std::string& deviceId) = 0; //初始化设备

    virtual bool startDevice(const std::string& deviceId) = 0; // 开始设备，
    virtual void shutdownDevice(const std::string& deviceId) = 0; //关闭设备


    virtual bool isDeviceOnline(const std::string& deviceId) const = 0 ;
    virtual DeviceHealthStatus getDeviceHealth(const std::string& deviceId) const = 0 ;

    virtual bool requestRecovery(const std::string& deviceId) = 0; //请求恢复
    virtual bool requestReboot(const std::string& deviceId) = 0; //请求重启
    
    //记录操作设备的情况
};


//设备引导器，负责设备的引导和初始化 Provisioner
class DeviceBootstrapper{
public:
    DeviceBootstrapper(IDeviceRepository& repository,IDeviceRuntimeBuilder& runtimeBuilder);

    ~DeviceBootstrapper();

    std::unordered_map<std::string, std::shared_ptr<DeviceRuntime>> bootstrapDevices();

    std::vector<DeviceDefinition> loadDeviceDefinitions();

private:

    IDeviceRepository& repository_;

    IDeviceRuntimeBuilder& runtimeBuilder_;
};



// 当前系统有哪些设备实例 
class DeviceRegistry{
public:
    DeviceRegistry();
    ~DeviceRegistry();

    bool registerDevice(const std::string& deviceId, std::shared_ptr<DeviceRuntime> runtime);
    bool unregisterDevice(const std::string& deviceId);

    bool contains(const std::string& deviceId) const;

    std::shared_ptr<DeviceRuntime> getDevice(const std::string& deviceId) const;

    std::vector<std::string> getAllDeviceIds() const;
    std::vector<std::shared_ptr<DeviceRuntime>> getAllDevices() const;

    void clear();
private:
    std::unordered_map<std::string, std::shared_ptr<DeviceRuntime>> devices_;


};



class DeviceStatusCache{
public:

    DeviceStatusCache();
    ~DeviceStatusCache();

    void updateStatus(const DeviceRuntimeStatus& status);

    void updateStatus(const std::string& deviceId, const DeviceRuntimeStatus& status);

    std::optional<DeviceRuntimeStatus> getStatus(const std::string& deviceId) const;

    bool contains(const std::string& deviceId) const;
    void remove(const std::string& deviceId);
    void clear();

    std::vector<DeviceRuntimeStatus> getAllStatuses() const;

    std::optional<DeviceRuntimeStatus> get(const std::string& deviceId) const;
private:
    std::unordered_map<std::string, DeviceRuntimeStatus> cache_;
    
};



//接收故障事件 执行恢复策略  控制恢复节奏 恢复失败时发出告警
/*
// 伪逻辑
1. 根据 deviceId 去 repository 查配置
2. 从 registry 里拿旧 runtime
3. 关掉旧 runtime
4. runtimeBuilder_.buildRuntime(definition)
5. 初始化并启动新 runtime
6. 重新注册进 registry
7. 更新 statusCache
*/
class DeviceRecoveryManager{ //60s 探活一次
public:
     DeviceRecoveryManager(DeviceRegistry& registry,
                          std::shared_ptr<DeviceStatusCache> statusCache,
                          IDeviceRepository& repository,
                          IDeviceRuntimeBuilder& runtimeBuilder
                        );

    ~DeviceRecoveryManager();

    bool submitRecoverTask(const RecoveryRequest& request);
    bool recoverDevice(const std::string& deviceId);
    bool rebootDevice(const std::string& deviceId);

    bool canRecover(const std::string& deviceId) const;
    void markRecovering(const std::string& deviceId);
    void markRecoveryFailed(const std::string& deviceId, const std::string& reason);
    void markRecoverySucceeded(const std::string& deviceId);

private:
    DeviceRegistry& registry_;
    std::shared_ptr<DeviceStatusCache> statusCache_;
    IDeviceRepository& repository_;
    IDeviceRuntimeBuilder& runtimeBuilder_;
};


//设备在线状态检测 设备健康状态采集  设备业务状态采集 把状态写入缓存 / 注册中心 发现异常时上报给恢复器 / 事件总线
// 短设备：
// 正常：
//     5s poll
// 失败：
//     1,2,4,8,16,32
// 长期失败：
//     60s 探活（永不停止）
// 极端异常：
//     → RecoveryManager（重建）
class DevicePoller{ //线程池 后续加上
public:
    DevicePoller(DeviceRegistry& registry,std::shared_ptr<DeviceStatusCache> statusCache,DeviceRecoveryManager& recoveryManager);

    ~DevicePoller();

    // void setRecoveryManager(DeviceRecoveryManager* recoveryManager);

    bool startPolling();
    void stopPolling();
    bool isPolling() const;

    void pollAllOnce();
    void pollSingleDevice(const std::string& deviceId);

private:
    void handlePollingSnapshot(const PollingSnapshot& snapshot);

private:
    DeviceRegistry& registry_;
    std::shared_ptr<DeviceStatusCache> statusCache_;
    DeviceRecoveryManager* recoveryManager_;

    std::atomic<bool> running_;

    std::thread pollThread_;
    
};



class DeviceRuntimeManager :public IDeviceRuntimeManager{
public:
    DeviceRuntimeManager(DeviceBootstrapper& bootstrapper,DevicePoller& polle,DeviceRecoveryManager& recoveryManager,DeviceRegistry& registry
        ,std::shared_ptr<DeviceStatusCache> statusCache,DriverManager& driverManager);

    ~DeviceRuntimeManager();

    bool initializeAll() override;
    bool startAll() override;
    void shutdownAll() override;

    bool initializeDevice(const std::string& deviceId) override;
    
    bool startDevice(const std::string& deviceId) override;
    void shutdownDevice(const std::string& deviceId) override;


    bool isDeviceOnline(const std::string& deviceId) const override;
    DeviceHealthStatus getDeviceHealth(const std::string& deviceId) const override;
    

    bool requestRecovery(const std::string& deviceId) override; 
    bool requestReboot(const std::string& deviceId) override; 

private:
    //初始化，设备
    bool checkDeviceType(DeviceType type);

    int64_t now();
private:
    DeviceBootstrapper& bootstrapper_; //构建设备
    DevicePoller& poller_; //轮询
    DeviceRecoveryManager& recoveryManager_; //恢复
    DeviceRegistry& registry_; //存放着设备表实例
    std::shared_ptr<DeviceStatusCache> statusCache_; //设备状态缓存，供查询和恢复管理器使用
    DriverManager& driverManager_;
};
