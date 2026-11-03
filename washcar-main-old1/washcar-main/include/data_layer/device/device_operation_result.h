#ifndef DEVICE_OPERATION_RESULT_H
#define DEVICE_OPERATION_RESULT_H

#include <string>

class DeviceOperationResult {
    public:
        DeviceOperationResult() = default;
        DeviceOperationResult(int code, const std::string message);

        bool operationBool() const {return (code_ == 0);} ;  // 0是成功 -1 是失败

        std::string getMessage() const {return message_;};
    private:
        int code_;
        std::string message_;
};

#endif  