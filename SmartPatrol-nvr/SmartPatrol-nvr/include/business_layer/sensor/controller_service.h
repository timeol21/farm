#ifndef CONTROLLER_SERVICE_H
#define CONTROLLER_SERVICE_H

#include "icontroller.h"
#include <memory>
#include <thread>
#include <atomic>
#include <functional>
#include <vector>
#include <string>
#include <mutex>

// ===================== 门锁设备实现（继承通用接口）=====================

class DoorLockDevice : public IControllableDevice {
public:
    DoorLockDevice(const std::string& id, int gpio_pin);
    
    bool initialize() override;
    bool turnOn() override;   // 对于门锁：解锁
    bool turnOff() override;  // 对于门锁：上锁
    DeviceState getState() const override;
    DeviceType getType() const override { return DeviceType::DOOR_LOCK; }
    std::string getId() const override { return id_; }
    bool isAvailable() const override;
    
private:
    std::string id_;
    int gpio_pin_;
    DeviceState state_;
    bool is_initialized_;
};

// ===================== 控制器服务接口 =====================

class IControllerService {
public:
    virtual ~IControllerService() = default;
    
    virtual bool initialize() = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
    
    // 通用设备管理
    virtual void addDevice(std::unique_ptr<IControllableDevice> device) = 0;
    virtual bool turnOn(const std::string& device_id) = 0;
    virtual bool turnOff(const std::string& device_id) = 0;
    virtual DeviceState getDeviceState(const std::string& device_id) const = 0;
    
    // 便捷方法：添加门锁
    virtual void addDoorLock(const std::string& lock_id, int gpio_pin) = 0;
    
    // 回调
    virtual void setStateCallback(std::function<void(const std::string& device_id,
                                                     DeviceType type,
                                                     DeviceState state,
                                                     long timestamp)> callback) = 0;
};

// ===================== 控制器服务实现类 =====================

class ControllerService : public IControllerService {
public:
    ControllerService();
    ~ControllerService();
    
    bool initialize() override;
    void start() override;
    void stop() override;
    bool isRunning() const override;
    
    // 通用设备管理
    void addDevice(std::unique_ptr<IControllableDevice> device) override;
    bool turnOn(const std::string& device_id) override;
    bool turnOff(const std::string& device_id) override;
    DeviceState getDeviceState(const std::string& device_id) const override;
    
    // 便捷方法
    void addDoorLock(const std::string& lock_id, int gpio_pin) override;
    
    // 回调
    void setStateCallback(std::function<void(const std::string& device_id,
                                            DeviceType type,
                                            DeviceState state,
                                            long timestamp)> callback) override;
    
private:
    void monitoringLoop();
    void notifyStateChange(const std::string& device_id, DeviceType type, DeviceState state);
    IControllableDevice* findDevice(const std::string& device_id);
    const IControllableDevice* findDevice(const std::string& device_id) const;
    
    std::vector<std::unique_ptr<IControllableDevice>> devices_;
    mutable std::mutex devices_mutex_;
    
    std::thread monitoring_thread_;
    std::atomic<bool> running_{false};
    std::function<void(const std::string&, DeviceType, DeviceState, long)> state_callback_;
};

#endif // CONTROLLER_SERVICE_H
