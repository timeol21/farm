#pragma once
#include "data_layer/device/device_status.h"
#include <unordered_map>
#include <cstdint>

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
    

    void updateFrom(const FxPlcStatus& other); 
    
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
};