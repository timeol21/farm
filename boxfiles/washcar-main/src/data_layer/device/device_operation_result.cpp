#include "data_layer/device/device_operation_result.h"

DeviceOperationResult::DeviceOperationResult(int code, const std::string message)
    : code_(code), message_(message) {

}
