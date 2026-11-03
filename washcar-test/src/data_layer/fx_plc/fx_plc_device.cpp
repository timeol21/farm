#include "data_layer/fx_plc/fx_plc_device.h"

#include <algorithm>

namespace {
// 单帧读超时的下限。再小的话 9600bps 下一帧都没收全就判失败了。
constexpr int kMinTimeoutMs = 50;
// 帧间静默下限。RTU 规定 >= 3.5 个字符时间；9600bps 下约 3.6ms，取 4ms 兜底。
constexpr int kMinFrameDelayMs = 4;
}  // namespace

FxPlcDevice::FxPlcDevice(const std::string& plcId,
                         const std::string& serialPort,
                         int baudRate,
                         int dataBits,
                         bool parityEven,
                         int stopBits,
                         int timeoutMs,
                         int frameDelayMs)
    : plcId_(plcId)
    , serialPort_(serialPort)
    , baudRate_(baudRate)
    , dataBits_(dataBits)
    , parityEven_(parityEven)
    , stopBits_(stopBits)
    , timeoutMs_(std::max(timeoutMs, kMinTimeoutMs))
    , frameDelayMs_(std::max(frameDelayMs, kMinFrameDelayMs)) {}

const std::string& FxPlcDevice::getPlcId() const { return plcId_; }
const std::string& FxPlcDevice::getSerialPort() const { return serialPort_; }
int FxPlcDevice::getBaudRate() const { return baudRate_; }
int FxPlcDevice::getDataBits() const { return dataBits_; }
bool FxPlcDevice::getParityEven() const { return parityEven_; }
int FxPlcDevice::getStopBits() const { return stopBits_; }
int FxPlcDevice::getTimeoutMs() const { return timeoutMs_; }
int FxPlcDevice::getFrameDelayMs() const { return frameDelayMs_; }
