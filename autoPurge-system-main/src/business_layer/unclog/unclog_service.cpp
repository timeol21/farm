#include "business_layer/unclog/unclog_service.h"
#include "common/log/log_manager.h"

UnclogService::UnclogService(
    IDeviceService& device,
    IDetectionService& detection,
    IUnclogDao& unclogDao,
    IAlarmDao& alarmDao
) :m_deviceService(device),m_detectionService(detection),m_unclogDao(unclogDao),m_alarmDao(alarmDao)
{

}

UnclogService::~UnclogService() {
    stopUnclogFlow();
}



void UnclogService::start() {
    auto raw = m_unclogDao.getDeviceInfo();
    if(!devices.build(raw)){
        return ;
    }

    ///这里拉流


    m_state = UnclogState::Scanning;

}

void UnclogService::stop() {

    //这里停止去拉流
    m_state = UnclogState::Scanning;

}



bool UnclogService::requestStart() {
    return true;
}     
    
bool UnclogService::requestStop() {
    return true;
}      

bool UnclogService::switchMode(SystemMode mode) {
    //这里要去看看设备是否在线的

    return true;
} 

UnclogStatus UnclogService::getStatus() const {
    return UnclogStatus();
}

AlarmInfo UnclogService::getAlarm() const {
    //去得到相应警告
    return AlarmInfo();
}

void UnclogService::onBlockDetected(const DetectionResult& result){
    if(m_state != UnclogState::Scanning){

        return ;
    }

    if (!canSwitchToUnclog()) return;


    m_state = UnclogState::Unclogging;

    startUnclogFlow();
}


//private

void UnclogService::startUnclogFlow(){
    if (m_state != UnclogState::Unclogging) {
        return; 
    }
    if(m_running.load()){
        return ;
    }
    m_worker = std::thread([this](){
        executeUnclogStep();
    });

}     
void UnclogService::stopUnclogFlow(){

    // 1. 先进入“停止中”状态（防止新动作进入）
    m_state = UnclogState::Stop_Unclogging;

    // 2. 暂停监听（AI / 传感器 / 事件流）
    if(m_worker.joinable()){
        m_worker.join();
    }
    m_running = false;

     // 3. 停止所有设备动作（高压水枪 / 阀门 / 机构）
    safeStopDevices();

    m_state = UnclogState::Unclogging;
}     

void UnclogService::executeUnclogStep(){

    // ===== 1. 启动泵 =====
    auto pumpCmd = DeviceCommand::Start(devices.getPump()->id);

    if (!executeWithCheck(pumpCmd, "启动高压泵失败")) return;

    // ===== 2. 打开所有阀 =====
    for (const auto& valve : devices.getValves()) {
        auto cmd = DeviceCommand::Open(valve.id);

        if (!executeWithCheck(cmd, "打开电磁阀失败")) return;
    }

    // ===== 3. 启动摆动机 =====
    for (const auto& motor : devices.getSwingMotors()) {
        auto cmd = DeviceCommand::Start(motor.id);

        if (!executeWithCheck(cmd, "启动摆动机失败")) return;
    }

    //执行

    // 监听
    monitorDuringUnclog();
}   


bool UnclogService::executeWithCheck(const DeviceCommand& cmd,const std::string& errMsg){
    auto result = m_deviceService.execute(cmd);

    if (!result.isOk()) {
        stopUnclogFlow();              // 停流程
        // m_alarmDao->raiseAlarm(errMsg);
        // logError(errMsg);

        return false;
    }

    return true;

}


bool UnclogService::canSwitchToUnclog() const{
    if(m_state == UnclogState::Scanning) return true;
    return false;
} 

   
void UnclogService::monitorDuringUnclog(){

    auto start = std::chrono::steady_clock::now();

    // auto time = m_unclogDao.
    //获取执行设备的信息 

    while(m_running && m_state == UnclogState::Unclogging){

        //1. 设备是否出现问题 （1.出现问题  2. 超时）关闭设备
        // auto result = m_deviceService.checkDeviceAbnormal();

        //2. 时间不能超过60秒
        auto now = std::chrono::steady_clock::now();
        // if(now - start > 60){
            // safeStopDevices();
            //日志
            //警告
            // return;
        // }

        //3.ai监测 和 雷达监测
    }

} 

    
void UnclogService::handleError(const std::string& reason){

}

void UnclogService::safeStopDevices(){
    // ===== 1. 停止泵 =====
    auto pumpCmd = DeviceCommand::Stop(devices.getPump()->id);

    if (!executeWithCheck(pumpCmd, "启动高压泵失败")) return;

    // ===== 2. 停止所有阀 =====
    for (const auto& valve : devices.getValves()) {
        auto cmd = DeviceCommand::Close(valve.id);

        if (!executeWithCheck(cmd, "打开电磁阀失败")) return;
    }

    // ===== 3. 停止摆动机 =====
    for (const auto& motor : devices.getSwingMotors()) {
        auto cmd = DeviceCommand::Close(motor.id);

        if (!executeWithCheck(cmd, "启动摆动机失败")) return;
    }
    
}