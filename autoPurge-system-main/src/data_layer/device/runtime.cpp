#include "data_layer/device/runtime/device_runtime.h"
#include "data_layer/device/runtime/device_session_context.h"
#include "data_layer/device/runtime/factory.h"
#include "data_layer/device/serial_port_parse.h"
SensorRuntime::SensorRuntime(std::shared_ptr<DeviceNode> node,std::shared_ptr<IDeviceDriver> driver)
    :node_(node),driver_(std::move(driver)), runtime_status(RuntimeDeviceStatus::OFFLINE)
{
    
}

SensorRuntime::~SensorRuntime(){
    stop();
}

const std::string& SensorRuntime::getId() const {
    return node_->definition.id;
}

DeviceType SensorRuntime::getType() const {
    return node_->definition.deviceTypeEnum;
}


bool SensorRuntime::isOnline() const {
    if(runtime_status != RuntimeDeviceStatus::EXCEPTION || runtime_status !=RuntimeDeviceStatus::OFFLINE) return true;
    return false;
}


bool SensorRuntime::initialize() {
    if(driver_->isConnected()) return false;
    runtime_status = RuntimeDeviceStatus::OFFLINE;
    return true;
}


void SensorRuntime::shutdown() {
     stop();
}

bool SensorRuntime::start() {
    runtime_status = RuntimeDeviceStatus::ONLINE;
}

bool SensorRuntime::stop() {
    
}

std::shared_ptr<IDeviceSessionContext> SensorRuntime::getSessionContext() {
    return nullptr;
}


void SensorRuntime::tick(SensorData& sensorData, int intervalSeconds){
    if (!driver_) return;

    const auto& def = node_->definition;
    if(def.bindingMode == RuntimeBindingMode::Independent){
        //这个就靠自己了
        SensorKind tyep = getSensorKind();
        if(tyep == SensorKind::TEMPERATURE_HUMIDITY){
            processTemperatureHumidity(sensorData,intervalSeconds);
        }
    }
    else if(def.bindingMode == RuntimeBindingMode::Dependent){
        //这个就得到看父亲
    }
    
}


SensorKind SensorRuntime::getSensorKind(){
    const auto& def = node_->definition;
    if(def.driverKey == "temperature_humidity") return SensorKind::TEMPERATURE_HUMIDITY;
    if(def.driverKey == "flood") return SensorKind::WATER_IMMERSION;
    if(def.driverKey == "pressure") return SensorKind::PRESSURE;
    if(def.driverKey == "smoke") return SensorKind::SMOKE_DETECTION;
}
    
    //温湿度
void SensorRuntime::processTemperatureHumidity(SensorData& data , int intervalSeconds){
    const auto& def = node_->definition;
    try {
        // ===== 1. 从 metadata 取参数 =====
        uint8_t slave = std::stoi(def.metadata.at("slave_addr"), nullptr, 0);
        uint16_t addr = std::stoi(def.metadata.at("reg_addr"), nullptr, 0);
        uint16_t count = std::stoi(def.metadata.at("read_regs"), nullptr, 0);
        // ===== 2. 构建请求 =====
        auto request = buildReadRequest(slave, addr, count);
        // ===== 3. 调用 driver =====
        std::vector<uint8_t> response;
        bool ok = driver_->transact(request, response, intervalSeconds);
       if (!ok) return;
        data = ModbusParser::parseTempHum(response);
       
    } catch (const std::exception& e) {
        
    }
}
    //水浸
void SensorRuntime::processWaterImmersion(SensorData& data , int intervalSeconds){

}
     //压力
void SensorRuntime::processPressure(SensorData& data , int intervalSeconds){

}
     //烟感
void SensorRuntime::processSmokeDetection(SensorData& data , int intervalSeconds){

}


std::vector<uint8_t> SensorRuntime::buildReadRequest(uint8_t slave,uint16_t addr,uint16_t count) {
    std::vector<uint8_t> req;

    req.push_back(slave);
    req.push_back(0x03); // 读保持寄存器

    req.push_back(addr >> 8);
    req.push_back(addr & 0xFF);

    req.push_back(count >> 8);
    req.push_back(count & 0xFF);

    uint16_t crc = modbus_crc16(req.data(), req.size());
    req.push_back(crc & 0xFF);
    req.push_back(crc >> 8);

    return req;
}

uint16_t SensorRuntime::modbus_crc16(const uint8_t* data, size_t len){
     uint16_t crc = 0xFFFF;
    for (size_t pos = 0; pos < len; pos++) {
        crc ^= (uint16_t)data[pos];
        for (int i = 0; i < 8; i++) {
            if (crc & 0x0001) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

PollingSnapshot SensorRuntime::poll(int interval) {

} 

bool SensorRuntime::shouldPoll(std::chrono::steady_clock::time_point now) const {
    return now >= nextPollTime_;
}

void SensorRuntime::updateNextPollTime(std::chrono::steady_clock::time_point now) {
    if (lastSnapshot_.success) {
        // 成功 → 恢复正常频率
        failCount_ = 0;
        interval_ = 5;
    } else {
        // 失败 → 指数退避
        failCount_++;
        interval_ = std::min(interval_ * 2, 60);
    }

    nextPollTime_ = now + std::chrono::seconds(interval_);
}

int SensorRuntime::getCurrentInterval() const {
    return interval_;
}




ActuatorRuntime::ActuatorRuntime(std::shared_ptr<DeviceNode> node,std::shared_ptr<IDeviceDriver> driver)
    :node_(node),driver_(std::move(driver)), runtime_status(RuntimeDeviceStatus::OFFLINE)
{

}

ActuatorRuntime::~ActuatorRuntime(){

}

const std::string& ActuatorRuntime::getId() const {

}

DeviceType ActuatorRuntime::getType() const {

}


bool ActuatorRuntime::isOnline() const {
     return true;
}


bool ActuatorRuntime::initialize() {
     return true;
}


void ActuatorRuntime::shutdown() {
    
}

bool ActuatorRuntime::start() {
     return true;
}

bool ActuatorRuntime::stop() {
     return true;
}

std::shared_ptr<IDeviceSessionContext> ActuatorRuntime::getSessionContext() {

}

void ActuatorRuntime::tick(SensorData& sensorData, int intervalSeconds){
    
}



PollingSnapshot ActuatorRuntime::poll(int interval) {

} 

bool ActuatorRuntime::shouldPoll(std::chrono::steady_clock::time_point now) const {

}

void ActuatorRuntime::updateNextPollTime(std::chrono::steady_clock::time_point now) {

}

int ActuatorRuntime::getCurrentInterval() const {

}

CameraRuntime::CameraRuntime(std::shared_ptr<DeviceNode> node,std::shared_ptr<IDeviceDriver> driver,std::shared_ptr<FrameBuffer> frameBuffer)
        : node_(node),driver_(std::move(driver)), runtime_status(RuntimeDeviceStatus::OFFLINE),  frameBuffer_(frameBuffer) 
{

}


CameraRuntime::~CameraRuntime(){
    shutdown();
}

const std::string& CameraRuntime::getId() const {
    return node_->definition.id;
}

DeviceType CameraRuntime::getType() const {
    return node_->definition.deviceTypeEnum;
}

bool CameraRuntime::isOnline() const {
    if(runtime_status != RuntimeDeviceStatus::EXCEPTION || runtime_status !=RuntimeDeviceStatus::OFFLINE) return true;
    return false;
}


bool CameraRuntime::initialize() {
     if (!driver_) return false;

    if (!driver_->connect()) {
        runtime_status = RuntimeDeviceStatus::OFFLINE;
        return false;
    }

    runtime_status = RuntimeDeviceStatus::ONLINE;
    return true;
}


void CameraRuntime::shutdown() {
    stop();

    if (driver_) {
        driver_->disconnect();
    }

    runtime_status = RuntimeDeviceStatus::OFFLINE;
}


bool CameraRuntime::start() {

    if (!driver_ || !driver_->isConnected()) {
        return false;
    }
    if (running_) return true;

    running_ = true;

    worker_ = std::thread([this](){
            while (running_) {
                std::shared_ptr<FrameData> frameData;
            if(!driver_->fetchFrame(frameData)){
                // 拉流失败 → 标记掉线
                runtime_status = RuntimeDeviceStatus::OFFLINE;

                // 可以考虑重连
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            runtime_status = RuntimeDeviceStatus::ONLINE;


            if (frameData && frameData->valid) {
                CameraChannelInfo info;
                info.deviceId = node_->definition.id;
                info.relevanceId = node_->definition.parentDeviceId;
                frameBuffer_->updateFrame(info,frameData);
            }
        }
    });
    
}

bool CameraRuntime::stop() {
    if (!running_) return true;

    running_ = false;

    if (worker_.joinable()) {
        worker_.join();
    }

    return true;
}    

std::shared_ptr<IDeviceSessionContext> CameraRuntime::getSessionContext() {
    
}

void CameraRuntime::tick(SensorData& sensorData, int intervalSeconds){
    
}

PollingSnapshot CameraRuntime::poll(int interval) {

} 

bool CameraRuntime::shouldPoll(std::chrono::steady_clock::time_point now) const {

}

void CameraRuntime::updateNextPollTime(std::chrono::steady_clock::time_point now) {

}

int CameraRuntime::getCurrentInterval() const {

}

PlcRuntime::PlcRuntime(std::shared_ptr<DeviceNode> node,std::shared_ptr<IDeviceDriver> driver)
    : node_(node),driver_(std::move(driver)), runtime_status(RuntimeDeviceStatus::OFFLINE)
{

}

PlcRuntime::~PlcRuntime(){

}


const std::string& PlcRuntime::getId() const{

}

DeviceType PlcRuntime::getType() const{

}


bool PlcRuntime::isOnline() const{
     return true;
}


bool PlcRuntime::initialize(){
     return true;
}


void PlcRuntime::shutdown(){

}

bool PlcRuntime::start(){
     return true;
}

bool PlcRuntime::stop() {
    return true;
}

std::shared_ptr<IDeviceSessionContext> PlcRuntime::getSessionContext() {

}

void PlcRuntime::tick(SensorData& sensorData, int intervalSeconds){
    
}

PollingSnapshot PlcRuntime::poll(int interval) {

} 

bool PlcRuntime::shouldPoll(std::chrono::steady_clock::time_point now) const {

}

void PlcRuntime::updateNextPollTime(std::chrono::steady_clock::time_point now) {

}

int PlcRuntime::getCurrentInterval() const {

}

RadarRuntime::RadarRuntime(std::shared_ptr<DeviceNode> node,std::shared_ptr<IDeviceDriver> driver)
    : node_(node),driver_(std::move(driver)), runtime_status(RuntimeDeviceStatus::OFFLINE)
{

}

RadarRuntime::~RadarRuntime(){

}

const std::string& RadarRuntime::getId() const {

}

DeviceType RadarRuntime::getType() const {

}


bool RadarRuntime::isOnline() const {
     return true;
}


bool RadarRuntime::initialize() {
     return true;
}


void RadarRuntime::shutdown() {

}

bool RadarRuntime::start() {
     return true;
}

bool RadarRuntime::stop() {
     return true;
}

std::shared_ptr<IDeviceSessionContext> RadarRuntime::getSessionContext() {

}

void RadarRuntime::tick(SensorData& sensorData, int intervalSeconds){
    
}


PollingSnapshot RadarRuntime::poll(int interval) {

} 

bool RadarRuntime::shouldPoll(std::chrono::steady_clock::time_point now) const {

}

void RadarRuntime::updateNextPollTime(std::chrono::steady_clock::time_point now) {

}

int RadarRuntime::getCurrentInterval() const {

}