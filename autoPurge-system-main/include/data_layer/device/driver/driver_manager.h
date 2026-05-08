#include "data_layer/device/device_data_object.h"
#include "data_layer/device/driver/device_driver.h"
#include <memory>
#include <unordered_map>
#include <mutex>
class DriverManager {
public:

    DriverManager() = default;
    ~DriverManager()= default;

    std::shared_ptr<IDeviceDriver> getOrCreate(const InterfaceDefinition& iface);

    //这里串口连接
    bool initializeAll();

    void shutdownAll();

    //这里单个串口连接

    //这里单个串口取消连接

private:
    std::unordered_map<std::string, std::shared_ptr<IDeviceDriver>> drivers_;
    std::mutex mutex_;
};