#ifndef ICONTROLLER_H
#define ICONTROLLER_H

#include <string>
#include <memory>

// ===================== 通用设备状态 =====================

// 开关型设备状态枚举（通用）1
enum class DeviceState {
    OFF = 0,        // 关闭/断开
    ON = 1,         // 开启/接通
    UNKNOWN = 2     // 未知
};

// 设备类型枚举（可扩展）
enum class DeviceType {
    DOOR_LOCK,      // 门锁
    // 后续可添加:
    // LIGHT,       // 灯光
    // RELAY,       // 继电器
    // ALARM,       // 警报器
    // FAN,         // 风扇
    // VALVE,       // 阀门
    OTHER           // 其他
};

// 状态转字符串
inline std::string deviceStateToString(DeviceState state) {
    switch (state) {
        case DeviceState::OFF:  return "OFF";
        case DeviceState::ON:   return "ON";
        default:                return "UNKNOWN";
    }
}

// 设备类型转字符串
inline std::string deviceTypeToString(DeviceType type) {
    switch (type) {
        case DeviceType::DOOR_LOCK: return "door_lock";
        // case DeviceType::LIGHT:  return "light";
        // case DeviceType::RELAY:  return "relay";
        default:                    return "other";
    }
}

// ===================== 通用可控设备接口 =====================

// 所有开关类型设备的基类接口
class IControllableDevice {
public:
    virtual ~IControllableDevice() = default;
    
    // 初始化设备
    virtual bool initialize() = 0;
    
    // 打开/开启设备
    virtual bool turnOn() = 0;
    
    // 关闭设备
    virtual bool turnOff() = 0;
    
    // 获取当前状态
    virtual DeviceState getState() const = 0;
    
    // 获取设备类型
    virtual DeviceType getType() const = 0;
    
    // 获取设备ID
    virtual std::string getId() const = 0;
    
    // 检查设备是否可用
    virtual bool isAvailable() const = 0;
};

#endif // ICONTROLLER_H
