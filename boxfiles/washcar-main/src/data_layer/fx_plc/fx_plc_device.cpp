#include "data_layer/fx_plc/fx_plc_device.h"

FxPlcDevice::FxPlcDevice(const std::string& plcId,
                         const std::string& serialPort,
                         int baudRate,
                         int dataBits,
                         bool parityEven,
                         int stopBits)
    : plcId_(plcId)
    , serialPort_(serialPort)
    , baudRate_(baudRate)
    , dataBits_(dataBits)
    , parityEven_(parityEven)
    , stopBits_(stopBits) {}

const std::string& FxPlcDevice::getPlcId() const { return plcId_; }
const std::string& FxPlcDevice::getSerialPort() const { return serialPort_; }
int FxPlcDevice::getBaudRate() const { return baudRate_; }
int FxPlcDevice::getDataBits() const { return dataBits_; }
bool FxPlcDevice::getParityEven() const { return parityEven_; }
int FxPlcDevice::getStopBits() const { return stopBits_; }