#include "business_layer/device/device_runtime_manager.h"

DeviceRuntimeManager::DeviceRuntimeManager(
    DeviceBootstrapper& bootstrapper,
    DevicePoller& poller,
    DeviceRecoveryManager& recoveryManager,
    DeviceRegistry& registry,
    std::shared_ptr<DeviceStatusCache> statusCache,
    DriverManager& driverManager
)
    : bootstrapper_(bootstrapper),
      poller_(poller),
      recoveryManager_(recoveryManager),
      registry_(registry),
      statusCache_(std::move(statusCache)),  // 智能指针最优写法
      driverManager_(driverManager)
{
    // 构造函数内部留空即可
}

DeviceRuntimeManager::~DeviceRuntimeManager(){

}

bool DeviceRuntimeManager::initializeAll() {
  registry_.clear();
  auto devices = bootstrapper_.bootstrapDevices();
  if(devices.empty()) return false;
  if(!driverManager_.initializeAll()){
    return false;
  }
  for(auto&[deviceId,runtime] : devices){
    registry_.registerDevice(deviceId,runtime);

    DeviceRuntimeStatus status;
      status.deviceId = deviceId;
      status.lastUpdateTime = now();
    // ===== 初始化 driver =====  
    
    // ===== 初始化 runtime =====
    
    if (!runtime->initialize()) {
      status.onlineStatus = DeviceOnlineStatus::Offline;
      status.healthStatus = DeviceHealthStatus(
          DeviceHealthStatus::Status::Exception,
          "initialize failed"
      );
      statusCache_->updateStatus(status);
      continue;
    } 
     // 初始化成功
    status.onlineStatus = DeviceOnlineStatus::Online;
    status.healthStatus = DeviceHealthStatus(
        DeviceHealthStatus::Status::Normal,
        "initialized"
    );

    statusCache_->updateStatus(status);
  }

  return true;
}
bool DeviceRuntimeManager::startAll() {

    for (auto& deviceId : registry_.getAllDeviceIds()) {

        auto runtime = registry_.getDevice(deviceId);
        if (!runtime) continue;

        // ===== 只启动长连接设备 =====
        if (!checkDeviceType(runtime->getType())) {
            continue;
        }

        auto statusOpt = statusCache_->getStatus(deviceId);
        DeviceRuntimeStatus status = statusOpt.value_or(DeviceRuntimeStatus{});
        status.deviceId = deviceId;
        status.lastUpdateTime = now();

         // ===== 启动 runtime =====
        if (!runtime->start()) {

            status.onlineStatus = DeviceOnlineStatus::Offline;
            status.healthStatus = DeviceHealthStatus(
                DeviceHealthStatus::Status::Exception,
                "start failed"
            );

            statusCache_->updateStatus(status);
            continue;
        }

        status.onlineStatus = DeviceOnlineStatus::Online;
        status.healthStatus = DeviceHealthStatus(
            DeviceHealthStatus::Status::Normal,
            "running"
        );

        statusCache_->updateStatus(status);
    }

    // ===== 启动轮询线程 =====
    if (!poller_.startPolling()) {
        return false;
    }

    return true;
}

int64_t DeviceRuntimeManager::now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

bool DeviceRuntimeManager::checkDeviceType(DeviceType type) {

    switch (type) {
        case DeviceType::CAMERA:   // 摄像头
        case DeviceType::RADAR:    // 雷达
            return true;  
        default:
            return false;
    }
}

void DeviceRuntimeManager::shutdownAll() {

     poller_.stopPolling();

    for (auto& deviceId : registry_.getAllDeviceIds()) {

        auto runtime = registry_.getDevice(deviceId);
        if (!runtime) continue;

        runtime->shutdown();

        DeviceRuntimeStatus status;
        status.deviceId = deviceId;
        status.lastUpdateTime = now();
        status.onlineStatus = DeviceOnlineStatus::Offline;
        status.healthStatus = DeviceHealthStatus(
            DeviceHealthStatus::Status::Normal,
            "shutdown"
        );

        statusCache_->updateStatus(status);
    }
}

bool DeviceRuntimeManager::initializeDevice(const std::string& deviceId) {
  
  return true;
}
bool DeviceRuntimeManager::startDevice(const std::string& deviceId) {
  return true;
}
void DeviceRuntimeManager::shutdownDevice(const std::string& deviceId) {

}


bool DeviceRuntimeManager::isDeviceOnline(const std::string& deviceId) const {
  // statusCache_->queryRuntimeStatus(deviceId);
  return true;
}
DeviceHealthStatus DeviceRuntimeManager::getDeviceHealth(const std::string& deviceId) const {
  return DeviceHealthStatus();
}

bool DeviceRuntimeManager::requestRecovery(const std::string& deviceId) {
  return true;
} 
bool DeviceRuntimeManager::requestReboot(const std::string& deviceId) {
  return true;
} 



DeviceBootstrapper::DeviceBootstrapper(IDeviceRepository& repository, IDeviceRuntimeBuilder& runtimeBuilder)
    : repository_(repository),
      runtimeBuilder_(runtimeBuilder)
{
    // 你自己的逻辑
}

DeviceBootstrapper::~DeviceBootstrapper(){

}


std::vector<DeviceDefinition> DeviceBootstrapper::loadDeviceDefinitions(){
  return {};
}

std::unordered_map<std::string, std::shared_ptr<DeviceRuntime>> DeviceBootstrapper::bootstrapDevices(){

  auto runtimes = runtimeBuilder_.buildRuntime();
  if(runtimes.empty()){
    LOG_ERROR("DeviceBootstrapper: 创建DeviceRuntime表出现问题");
  }
  return runtimes;
}



DeviceRegistry::DeviceRegistry(){

}
DeviceRegistry::~DeviceRegistry(){

}

bool DeviceRegistry::registerDevice(const std::string& deviceId, std::shared_ptr<DeviceRuntime> runtime){
  return true;
}
bool DeviceRegistry::unregisterDevice(const std::string& deviceId){
  return true;
}

bool DeviceRegistry::contains(const std::string& deviceId) const{
  return true;
}

std::shared_ptr<DeviceRuntime> DeviceRegistry::getDevice(const std::string& deviceId) const{
  return nullptr;
}

std::vector<std::string> DeviceRegistry::getAllDeviceIds() const{
  return {};
}
std::vector<std::shared_ptr<DeviceRuntime>> DeviceRegistry::getAllDevices() const{
  return {};
}

void DeviceRegistry::clear(){

}


DeviceStatusCache::DeviceStatusCache(){

}
DeviceStatusCache::~DeviceStatusCache(){

}

void DeviceStatusCache::updateStatus(const DeviceRuntimeStatus& status){

}

void DeviceStatusCache::updateStatus(const std::string& deviceId, const DeviceRuntimeStatus& status){

}

std::optional<DeviceRuntimeStatus> DeviceStatusCache::getStatus(const std::string& deviceId) const{
   return std::nullopt;
}

bool DeviceStatusCache::contains(const std::string& deviceId) const{
  return true;
}
void DeviceStatusCache::remove(const std::string& deviceId){

}
void DeviceStatusCache::clear(){

}

std::vector<DeviceRuntimeStatus> DeviceStatusCache::getAllStatuses() const{
  return {};
}

std::optional<DeviceRuntimeStatus> DeviceStatusCache::get(const std::string& deviceId) const{
   return std::nullopt;
}


DeviceRecoveryManager::DeviceRecoveryManager(
    DeviceRegistry& registry,
    std::shared_ptr<DeviceStatusCache> statusCache,
    IDeviceRepository& repository,
    IDeviceRuntimeBuilder& runtimeBuilder
)   : registry_(registry),
      statusCache_(std::move(statusCache)),
      repository_(repository),
      runtimeBuilder_(runtimeBuilder)
{
    // 构造函数内部可以空着，不用写任何东西
}

DeviceRecoveryManager::~DeviceRecoveryManager(){

}

bool DeviceRecoveryManager::submitRecoverTask(const RecoveryRequest& request){
  return true;
}
bool DeviceRecoveryManager::recoverDevice(const std::string& deviceId){
  return true;
}
bool DeviceRecoveryManager::rebootDevice(const std::string& deviceId){
  return true;
} 

bool DeviceRecoveryManager::canRecover(const std::string& deviceId) const{
  return true;
}
void DeviceRecoveryManager::markRecovering(const std::string& deviceId){

}

void DeviceRecoveryManager::markRecoveryFailed(const std::string& deviceId, const std::string& reason){

}

void DeviceRecoveryManager::markRecoverySucceeded(const std::string& deviceId){

}


DevicePoller::DevicePoller(
    DeviceRegistry& registry,
    std::shared_ptr<DeviceStatusCache> statusCache,
    DeviceRecoveryManager& recoveryManager
)   : registry_(registry),
      statusCache_(std::move(statusCache)),
      recoveryManager_(&recoveryManager)  
{
    
}

DevicePoller::~DevicePoller(){

}

    

bool DevicePoller::startPolling(){
  running_ = true;

  pollThread_ = std::thread([this](){
    while(running_){
      pollAllOnce();
      std::this_thread::sleep_for(std::chrono::seconds(1));//休息
    }
  });

  return true;
}
void DevicePoller::stopPolling(){
  running_ = false;
  if(pollThread_.joinable()){
    pollThread_join();
  }
}

bool DevicePoller::isPolling() const{
  if(running_) return true;
  return false;
  
}

void DevicePoller::pollAllOnce(){
  auto devices = registry_.getAllDevices();
  auto now = std::chrono::steady_clock::now();

    for (auto& runtime : devices) {
        if (!runtime) continue;

        // 关键：是否到时间
        if (!runtime->shouldPoll(now)) continue;

        auto snapshot = runtime->poll(runtime->getCurrentInterval());

        handlePollingSnapshot(snapshot);

        runtime->updateNextPollTime(now); // 内部做退避 or 恢复
    }
}
void DevicePoller::pollSingleDevice(const std::string& deviceId){

}


void DevicePoller::handlePollingSnapshot(const PollingSnapshot& snapshot){
    // 更新状态缓存
    DeviceRuntimeStatus status;
    status.deviceId = snapshot.deviceId;
    status.lastUpdateTime = now();

    if (snapshot.success) {
        status.onlineStatus = snapshot.online ? DeviceOnlineStatus::Online : DeviceOnlineStatus::Offline;
        status.healthStatus = DeviceHealthStatus(
            DeviceHealthStatus::Status::Normal,
            snapshot.online ? "Device is online" : "Device is offline"
        );
    } else {
        status.onlineStatus = DeviceOnlineStatus::Offline;
        status.healthStatus = DeviceHealthStatus(
            DeviceHealthStatus::Status::Exception,
            "Polling failed"
        );

        // 上报恢复器
        recoveryManager_->submitRecoverTask({snapshot.deviceId, "Polling failure"});
    }

    statusCache_->updateStatus(status);
}