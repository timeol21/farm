#include "business_layer/device/device_service.h"

int main(int argc, char* argv[]) {

    TempHumidSensorAcquisitionTask tempSensorTask = TempHumidSensorAcquisitionTask(1, 10);
    SerialDirectDeviceAcquisitionTask serialDeviceTask = SerialDirectDeviceAcquisitionTask(tempSensorTask); 

    SolenoidAcquisitionTask solenoidTask = SolenoidAcquisitionTask(0,10);           // 默认构造或自定义参数
    InfraredSensorAcquisitionTask infraredTask = InfraredSensorAcquisitionTask(4,10);
    WaterLevelSensorAcquisitionTask waterLevelTask = WaterLevelSensorAcquisitionTask(5,10);
    SmokeDetectorAcquisitionTask smokeTask = SmokeDetectorAcquisitionTask(6,10);


    PlcDeviceAcquisitionTask plcTask = PlcDeviceAcquisitionTask(solenoidTask,infraredTask,waterLevelTask,smokeTask);

    DoorLockAcquisitionTask doorLockTask = DoorLockAcquisitionTask(3,10);
    GPIODeviceAcquisitionTask gpioTask  = GPIODeviceAcquisitionTask(doorLockTask);

    //设备任务类
    DeviceAcquisitionTask deviceAcquisitionTask = DeviceAcquisitionTask(serialDeviceTask, plcTask,gpioTask);

    PlcDevice plcDevice = PlcDevice(10,"plc_001","111","?","0x01");
    std::vector<SolenoidValue> solenoidvalues ;
    SolenoidValue solenoid = SolenoidValue(0,"solenoid_001","001","00","00","00","00","00");
    solenoidvalues.push_back(solenoid);

    InfraredSensor infraredSensor = InfraredSensor(4,"11","11","211","1221","22","121");
    std::vector<InfraredSensor>  infraredSensors;
    infraredSensors.push_back(infraredSensor);

    std::vector<PlcSmokeDetector> smokeDetectors;
    PlcSmokeDetector smokeDetector = PlcSmokeDetector(5,"11","11","211","1221","22","121");
    smokeDetectors.push_back(smokeDetector);

    std::vector<PlcWaterLevelSensor> waterLevelSensors;
    PlcWaterLevelSensor waterSensor = PlcWaterLevelSensor(6,"11","11","211","1221","22","121");

    PlcInstance plcInsatnce = PlcInstance(plcDevice,SerialConfig(1,1,1,"11"),solenoidvalues,infraredSensors,smokeDetectors,waterLevelSensors);
    std::unordered_map<std::string, PlcInstance> plcMap;
    plcMap.emplace("plc_001",plcInsatnce);
    PlcInstanceSet plcInstances = PlcInstanceSet(plcMap);

    
    std::unordered_map<std::string,std::unique_ptr<Camera> >  cameras;
    cameras.emplace("camera_00x",std::make_unique<Camera> ("camera_00x","camera","") );
    CameraInstanceSet cameraInstances(std::move(cameras) );

    std::vector<DoorLock> doorLocks;
    doorLocks.reserve(5);
    doorLocks.push_back(DoorLock(3,"1","!",1,1,1,"1","1",1,"11"));
    GPIODeviceInstanceSet gpioInstanceSet = GPIODeviceInstanceSet(doorLocks);

    std::vector<TempHumidSensor> sensors;
    sensors.reserve(5);
    sensors.push_back(TempHumidSensor(1,"1","1","1","1","1",1,SerialConfig()));
    SerialDirectDeviceInstanceSet serialInstances = SerialDirectDeviceInstanceSet(sensors);

    DeviceManageService deviceManageService(std::move(plcInstances),std::move(cameraInstances),std::move(gpioInstanceSet),std::move(serialInstances));

    DeviceStatusCache deviceStatusCache = DeviceStatusCache();

    RealTimeFrameCache realTimeFrameCache = RealTimeFrameCache();

    DeviceService deviceService(deviceManageService,deviceStatusCache,deviceAcquisitionTask,realTimeFrameCache);
}