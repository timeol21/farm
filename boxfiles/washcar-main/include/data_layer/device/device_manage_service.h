#include "data_layer/plc/fx_plc_instance_set.h"

class DeviceManageService {
public:
    // 原有构造函数增加 fxPlcSet 参数
    DeviceManageService(PlcInstanceSet&& plcInstances,
                        CameraInstanceSet&& cameraInstances,
                        GPIODeviceInstanceSet&& gpioInstanceSet,
                        SerialDirectDeviceInstanceSet&& serialInstances,
                        FxPlcInstanceSet&& fxPlcSet);   // 新增

    // 新增 FX PLC 操作接口
    bool openFxSolenoid(const std::string& plcId, int yOctal);
    bool closeFxSolenoid(const std::string& plcId, int yOctal);
    bool readFxRegister(const std::string& plcId, int dNumber, uint16_t& value);
    // ... 其他需要的方法

private:
    FxPlcInstanceSet fxPlcSet_;
};