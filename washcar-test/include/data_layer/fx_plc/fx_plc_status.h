#pragma once
#include "data_layer/device/device_status.h"
#include <unordered_map>
#include <cstdint>
#include <chrono>

class FxPlcStatus : public DeviceStatus {
public:
    FxPlcStatus() = default;
    explicit FxPlcStatus(const std::string& plcId);
    
    void updateYBit(int yOctal, bool state);
    void updateMBit(int mDecimal, bool state);
    void updateDRegister(int dNumber, uint16_t value);
    void updateXBit(int xOctal, bool state);
    void updateSBit(int sDecimal, bool state);  
    bool getSBit(int sDecimal, bool& state) const;
    bool getXBit(int xOctal, bool& state) const;
    bool getYBit(int yOctal, bool& state) const;
    bool getMBit(int mDecimal, bool& state) const;
    bool getDRegister(int dNumber, uint16_t& value) const;

    //点位状态变化标志：设置由设备服务采集时完成，清除由洗车业务完成
    bool isChanged() const { return changed_; }
    void setChanged() { changed_ = true; }
    void clearChanged() { changed_ = false; }

    void updateFrom(const FxPlcStatus& other); 

    void updateTimestamp() {
        lastUpdateTime_ = std::chrono::steady_clock::now();
    }
    //获取时间戳
    std::chrono::steady_clock::time_point getLastUpdateTime() const {
        return lastUpdateTime_;
    }

    //时间戳的字符串表示
    std::string getLastUpdateTimeStr() const {
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
        return std::to_string(millis);
    }
    
    const std::string& getPlcId() const { return plcId_; }
    const std::unordered_map<int, bool>& getYMap() const { return yStatusMap_; }
    const std::unordered_map<int, bool>& getMMap() const { return mStatusMap_; }
    const std::unordered_map<int, uint16_t>& getDMap() const { return dRegisterMap_; }
    const std::unordered_map<int, bool>& getXMap() const { return xStatusMap_; }
    const std::unordered_map<int, bool>& getSMap() const { return sStatusMap_; }

private:
    std::string plcId_;
    std::unordered_map<int, bool> yStatusMap_;      // key: 八进制地址
    std::unordered_map<int, bool> mStatusMap_;      // key: 十进制地址
    std::unordered_map<int, uint16_t> dRegisterMap_;
    std::unordered_map<int, bool> xStatusMap_;    
    std::unordered_map<int, bool> sStatusMap_;  
    bool changed_ = false;
    std::chrono::steady_clock::time_point lastUpdateTime_;
};