#include "business_layer/command/command_object.h"
#include <sstream>  
#include <atomic>



Command::Command(std::string id, CommandType type, std::string devId)
        : cmdId(std::move(id)), cmdType(type), deviceId(std::move(devId)),
          cmdState(CommandState::STATE_IDLE) {auto now = std::chrono::system_clock::now();
                                              createTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                                              executeTime = 0; // 执行时间默认为0
}

Command Command::createOperateSolenoid(const SolenoidValveOperation& solenoid)
{
    std::string cmdId = createCmdId();

    CommandType type;

    if (solenoid.getCmd() == "open") {
        type = CommandType::TYPE_SOLENOID_OPEN;
    } 
    else if (solenoid.getCmd() == "close") {
        type = CommandType::TYPE_SOLENOID_CLOSE;
    } 
    else {
        throw std::invalid_argument("Invalid solenoid command");
    }

    return Command(cmdId, type, solenoid.getDeviceId());
}

Command Command::createHistoricalVideo(DownloadHistoricalVideo& download) {
    
    return Command(createCmdId(), CommandType::TYPE_CAMERA_HISTORY, "DEVICE_DEFAULT");;
}

Command Command:: createOperateSolenoidResult(const DeviceOperationResult& solenoid,const SolenoidValveOperation& operation){
    std::string cmdId = createCmdId();

    CommandType type;

    if (operation.getCmd() == "open") {
        type = CommandType::TYPE_SOLENOID_OPEN;
    } 
    else if (operation.getCmd() == "close") {
        type = CommandType::TYPE_SOLENOID_CLOSE;
    } 
    else {
        throw std::invalid_argument("Invalid solenoid command");
    }

    return Command(cmdId, type, operation.getDeviceId());
}

Command Command::createWashCarStart(const WashCarOperation& operation) {
    std::string cmdId = createCmdId();
    return Command(cmdId, CommandType::TYPE_WASHCAR_START, operation.getBoxNo());
}

Command Command::createWashCarStop(const WashCarOperation& operation) {
    std::string cmdId = createCmdId();
    return Command(cmdId, CommandType::TYPE_WASHCAR_STOP, operation.getBoxNo());
}

Command Command::createWashCarReset(const WashCarOperation& operation) {
    std::string cmdId = createCmdId();
    return Command(cmdId, CommandType::TYPE_WASHCAR_RESET, operation.getBoxNo());
}



std::string Command::getCmdId() const { return cmdId; }
CommandType Command::getCmdType() const { return cmdType; }
CommandState Command::getCmdState() const { return cmdState; }
std::string Command::getDeviceId() const { return deviceId; }
std::string Command::getCmdContent() const { return cmdContent; }

long long Command::getCreateTime() const { return createTime; }
long long Command::getExecuteTime() const { return executeTime; }

void Command::setCmdState(CommandState state) {
    cmdState = state;
}

std::string Command::createCmdId(){
    static std::atomic<int> id{1};

    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()).count();

    std::stringstream ss;
    ss << "CMD_" << ms << "_" << id++;

    return ss.str();
}