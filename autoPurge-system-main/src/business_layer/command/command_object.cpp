#include "business_layer/command/command_object.h"
#include <sstream>  
#include <atomic>



Command::Command(std::string id, CommandType type, std::string devId,CommandState state)
        : cmdId(std::move(id)), cmdType(type), deviceId(std::move(devId)),
          cmdState(state) {auto now = std::chrono::system_clock::now();
                                              createTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                                              executeTime = 0; // 执行时间默认为0
}

// Command Command::createOperateSolenoid(const SolenoidValveOperation& solenoid)
// {
//     std::string cmdId = createCmdId();

//     CommandType type;

//     if (solenoid.getCmd() == "open") {
//         type = CommandType::TYPE_SOLENOID_OPEN;
//     } 
//     else if (solenoid.getCmd() == "close") {
//         type = CommandType::TYPE_SOLENOID_CLOSE;
//     } 
//     else {
//         throw std::invalid_argument("Invalid solenoid command");
//     }

//     return Command(cmdId, type, solenoid.getDeviceId(),CommandState::STATE_IDLE);
// }

// Command Command::createHistoricalVideo(DownloadHistoricalVideo& download) {
    
//     return Command(createCmdId(), CommandType::TYPE_CAMERA_HISTORY, "DEVICE_DEFAULT",CommandState::STATE_IDLE);
// }

// Command Command:: createOperateSolenoidResult(const int result,const SolenoidValveOperation& operation){
//     std::string cmdId = createCmdId();

//     CommandType type;
//     CommandState state;
//     if (operation.getCmd() == "open") {
//         type = CommandType::TYPE_SOLENOID_OPEN;
//     } 
//     else if (operation.getCmd() == "close") {
//         type = CommandType::TYPE_SOLENOID_CLOSE;
//     } 
//     else {
//         throw std::invalid_argument("Invalid solenoid command");
//     }
//     if(result == 1) state = CommandState::STATE_SUCCESS;
//     else state = CommandState::STATE_FAILED;



//     return Command(cmdId, type, operation.getDeviceId(),state);
// }

// Command Command::createRetrieveSolenoidStatusResult(const SolenoidStatus& status) {
//     std::string cmdId = createCmdId();
//     CommandType type = CommandType::TYPE_DEVICE_READ;
//     CommandState state;
//     if(status.getStatus() == "UNKNOW") state = CommandState::STATE_FAILED;
//     else state = CommandState::STATE_SUCCESS;

//     return Command(cmdId, type, status.getDeviceId(), state);
// }

// Command Command::createRetrieveTempHumidSensorStatusResult(const TempHumidSensorStatus& status) {
//     std::string cmdId = createCmdId();
//     CommandType type = CommandType::TYPE_DEVICE_READ;
//     CommandState state;
//     if(status.getStatus() == TempHumidStatus::ABNORMAL) state = CommandState::STATE_FAILED;
//     else state = CommandState::STATE_SUCCESS;
//     return Command(cmdId, type, status.getDeviceId(), state);

// }

// Command Command::createRetrieveInfraredSensorStatusResult(const InfraredSensorStatus& status) {
//     std::string cmdId = createCmdId();
//     CommandType type = CommandType::TYPE_DEVICE_READ;
//     CommandState state;
//     if(status.getStatus() == "UNKNOW" ) state = CommandState::STATE_FAILED;
//     else state = CommandState::STATE_SUCCESS;
//     return Command(cmdId, type, status.getDeviceId(), state);
// }

// Command Command::createRetrieveSmokeDetectorStatusResult(const SmokeDetectorStatus& status) {
//     std::string cmdId = createCmdId();
//     CommandType type = CommandType::TYPE_DEVICE_READ;
//     CommandState state;
//     if(status.getStatus() == "UNKNOW" ) state = CommandState::STATE_FAILED;
//     else state = CommandState::STATE_SUCCESS;
//     return Command(cmdId, type, status.getDeviceId(), state);
// }

// Command Command::createRetrieveWaterLevelSensorStatusResult(const WaterLevelSensorStatus& status) {
//     std::string cmdId = createCmdId();
//     CommandType type = CommandType::TYPE_DEVICE_READ;
//     CommandState state;
//     if(status.getStatus() == "UNKNOW" ) state = CommandState::STATE_FAILED;
//     else state = CommandState::STATE_SUCCESS;
//     return Command(cmdId, type, status.getDeviceId(), state);
// }

bool Command::isEmpty() const {
    if(cmdId.empty()){
        return true;
    }
    return false;
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