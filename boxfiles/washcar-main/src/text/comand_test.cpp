#include <csignal>
#include "business_layer/lobby/lobby_service.h"
#include "presentation_layer/http_service.h"
#include "business_layer/command/mqtt_command.h"
#include "business_layer/command/mqtt/mqtt_service.h"
#include "business_layer/command/command_service.h"
#include "common/log/log_manager.h"
#include "data_layer/fx_plc/fx_plc_device.h"
#include "data_layer/fx_plc/fx_plc_instance.h"
#include "data_layer/fx_plc/fx_plc_instance_set.h"
#include "business_layer/washcar/washcar_service.h"
#include "business_layer/device/device_change_listener.h"

//testplc
#include <fcntl.h>      // 提供 open, O_RDWR, O_NOCTTY, O_NONBLOCK
#include <cstring>      // 提供 strerror
// 全局标志：控制程序是否继续运行
bool g_running = true;

// 信号处理函数：捕获Ctrl+C等退出信号，优雅退出
void signalHandler(int signum) {
    // 根据signum判断信号类型，输出更友好的日志
    if (signum == SIGINT) {
        std::cout << ">>> 接收到【Ctrl+C中断信号】(编号: " << signum << ")，正在优雅关闭系统..." << std::endl;
    } else if (signum == SIGTERM) {
        std::cout << ">>> 接收到【进程终止信号】(编号: " << signum << ")，正在优雅关闭系统..." << std::endl;
    } else {
        std::cout << ">>> 接收到未知信号(编号: " << signum << ")，正在优雅关闭系统..." << std::endl;
    }
    g_running = false;
}
//监听测试
class TestChangeListener : public IDeviceChangeListener {
public:
    void onDeviceChanged(const ChangeRecord& change) override {
        std::cout << "[变化检测] 设备: " << change.deviceId
                  << "  点位: " << change.pointName
                  << "  旧值: " << change.oldValue
                  << "  新值: " << change.newValue
                  << "  类型: " << change.deviceType
                  << std::endl;
    }
};
int main(int argc, char* argv[]) {
    // 注册信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    std::cout << "=============================================" << std::endl;
    std::cout << "正在启动项目中....." << std::endl;
    std::cout << "\n>>> 开始与 FX PLC 通讯..." << std::endl;
    FxPlcDevice fxDevice("fx_plc_01", "/dev/ttyUSB11", 9600, 7, true, 1);
    auto fxPlc = std::make_unique<FxPlcInstance>(fxDevice);
    FxPlcInstanceSet fxPlcSet;
    fxPlcSet.addPlc(std::move(fxPlc));
    //设备采集任务
    //DeviceAcquisitionTask deviceAcquisitionTask = DeviceAcquisitionTask(fxPlcSet);
    


    // ====================== 设备任务初始化（原代码不变） ======================
   // CameraInstanceSet cameraInstances;                 // 空集合
    //GPIODeviceInstanceSet gpioInstanceSet;             // 空集合
    //SerialDirectDeviceInstanceSet serialInstances;
    // TempHumidSensorAcquisitionTask tempSensorTask = TempHumidSensorAcquisitionTask(1, 10);
    //SerialDirectDeviceAcquisitionTask serialDeviceTask = SerialDirectDeviceAcquisitionTask(tempSensorTask); 

    //SolenoidAcquisitionTask solenoidTask = SolenoidAcquisitionTask(0,10);           
    //InfraredSensorAcquisitionTask infraredTask = InfraredSensorAcquisitionTask(4,10);
    //WaterLevelSensorAcquisitionTask waterLevelTask = WaterLevelSensorAcquisitionTask(5,10);
   // SmokeDetectorAcquisitionTask smokeTask = SmokeDetectorAcquisitionTask(6,10);

    //PlcDeviceAcquisitionTask plcTask = PlcDeviceAcquisitionTask(solenoidTask,infraredTask,waterLevelTask,smokeTask);
    //DoorLockAcquisitionTask doorLockTask = DoorLockAcquisitionTask(3,10);
    //GPIODeviceAcquisitionTask gpioTask  = GPIODeviceAcquisitionTask(doorLockTask);

    //DeviceAcquisitionTask deviceAcquisitionTask = DeviceAcquisitionTask(serialDeviceTask, plcTask,gpioTask);
    

    //PlcDevice plcDevice = PlcDevice(10,"plc_001","111","?","0x01");
    //std::vector<SolenoidValue> solenoidvalues ;
    //SolenoidValue solenoid = SolenoidValue(0,"solenoid_001","001","00","00","00","00","00");
    //solenoidvalues.push_back(solenoid);

    // InfraredSensor infraredSensor = InfraredSensor(4,"11","11","211","1221","22","121");
    // std::vector<InfraredSensor>  infraredSensors;
    // infraredSensors.push_back(infraredSensor);

    // std::vector<PlcSmokeDetector> smokeDetectors;
    // PlcSmokeDetector smokeDetector = PlcSmokeDetector(5,"11","11","211","1221","22","121");
    // smokeDetectors.push_back(smokeDetector);

    // std::vector<PlcWaterLevelSensor> waterLevelSensors;
    // PlcWaterLevelSensor waterSensor = PlcWaterLevelSensor(6,"11","11","211","1221","22","121");

    // PlcInstance plcInsatnce = PlcInstance(plcDevice,SerialConfig(1,1,1,"11"),solenoidvalues,infraredSensors,smokeDetectors,waterLevelSensors);
    // std::unordered_map<std::string, PlcInstance> plcMap;
    // plcMap.emplace("plc_001",plcInsatnce);
    // PlcInstanceSet plcInstances = PlcInstanceSet(plcMap);
    //三菱fx3u
   //测试 PLC 通讯
    // ========== 新增：手动测试 FX PLC 通讯（带详细错误） ==========
    

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

    
    DeviceManageService deviceManageService(
        //std::move(plcInstances),
        std::move(fxPlcSet),
        std::move(cameraInstances),
        std::move(gpioInstanceSet),
        std::move(serialInstances)
         );

        //fx采集
        DeviceAcquisitionTask deviceAcquisitionTask(deviceManageService.getFxPlcSet());
    DeviceStatusCache deviceStatusCache = DeviceStatusCache();

    //监听测试
    auto testListener = std::make_shared<TestChangeListener>();
    deviceStatusCache.addListener(testListener);


    RealTimeFrameCache realTimeFrameCache = RealTimeFrameCache();

    DeviceService deviceService(deviceManageService, deviceStatusCache, deviceAcquisitionTask, realTimeFrameCache);
    // ====================== 日志系统初始化 + 失败处理 ======================
    auto queue = std::make_shared<BlockingQueue>();
    auto logger = std::make_shared<AsyncLogger>(queue);
    auto fileSink = std::make_shared<FileSink>("log");
    logger->addSink(fileSink);

    // 日志启动失败直接退出
    if (!logger->start()) {
        std::cerr << ">>> 错误：日志系统启动失败，程序退出" << std::endl;
        return -1;
    }
    LoggerManager::instance().registerLogger("work", logger);
    std::cout << ">>> 日志系统启动成功" << std::endl;

    // ====================== 数据库初始化 + 失败处理 ======================
    auto& dbManager = DatabaseManager::instance();
    if (!dbManager.init("/home/lin/Desktop/Framework/include/common/database")) {
        std::cerr << ">>> 错误：数据库初始化失败，程序退出" << std::endl;
        logger->stop();  // 清理已启动的日志
        return -1;
    }
    std::cout << ">>> 数据库初始化成功" << std::endl;

    CommandDao commandDao(dbManager.getCommandDB());
    NetworkService* networkService = nullptr;
    SafetyService safetyService;
    CommandService commandService(networkService, commandDao);
    WashCarService washcarService(deviceService);
    Timer timer;
    LobbyService lobbyService(safetyService, commandService, deviceService,washcarService, timer, deviceStatusCache);

    MQTTCommandController mqttController = MQTTCommandController(lobbyService);
    HTTPCommandController httpController = HTTPCommandController(lobbyService);

    MqttProtocol mqttProtocol;
    MqttService mqttService("192.168.31.10", 1883, "admin", "ac123",mqttController, mqttProtocol);
    commandService.immitDependence(mqttService); 


    // ====================== MQTT 启动 + 失败处理（核心需求） ======================
    std::cout << ">>> 正在启动 MQTT 服务..." << std::endl;
    if (!mqttService.start()) {
        std::cerr << ">>> 错误：MQTT 服务启动失败，程序退出" << std::endl;
        // 只清理已经启动的资源，不调用 stop()
        logger->stop();
        return -1;
    }



    std::cout << ">>> MQTT 服务启动成功" << std::endl;
    
    // ====================== 所有服务启动完成 ======================
    std::cout << std::endl;
    std::cout << "=============================================" << std::endl;
    std::cout << ">>> 系统启动成功！服务已就绪 (进程 ID: " << getpid() << ")" << std::endl; 
    std::cout << ">>> 按 Ctrl+C 停止服务" << std::endl;
    std::cout << "=============================================" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(25));
    std::cout << "\n>>> [测试] forceM 置位 M100 ..." << std::endl;
    bool forceRet = deviceManageService.forceM("fx_plc_01", 100, true);
    std::cout << "forceM M100 返回: " << (forceRet ? "成功" : "失败") << std::endl;
    // 等待采集线程 (0.5s 周期) 捕捉变化并更新缓存
    std::this_thread::sleep_for(std::chrono::seconds(2));

// test
//DeviceStatusQuery query;


    // 主循环
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));


        // std::this_thread::sleep_for(std::chrono::milliseconds(500));
        // std::cout << "\n>>> [测试] 缓存读取" << std::endl;

        //     BoxDeviceStatus directStatus = deviceService.viewAllDeviceStatus();

        //     const auto& fxList = directStatus.getFxPlcStatusList();
        //     std::cout << ">>> [测试] FX PLC 数量: " << fxList.size() << std::endl;

        //     for (const auto& fx : fxList) {
        //         std::cout << "  PLC ID: " << fx.getDeviceId() << std::endl;

        //         bool y50 = false, y51 = false, y74 = false,x23=false,m1=false,s0=false;
        //         if (fx.getYBit(50, y50))
        //             std::cout << "    Y50 = " << (y50 ? "ON" : "OFF") << std::endl;
        //         if (fx.getYBit(51, y51))
        //             std::cout << "    Y51 = " << (y51 ? "ON" : "OFF") << std::endl;
        //         if (fx.getYBit(74, y74))
        //             std::cout << "    Y74 = " << (y74 ? "ON" : "OFF") << std::endl;
        //         if(fx.getXBit(23, x23))
        //             std::cout << "    X23 = " << (x23 ? "ON" : "OFF") << std::endl;
        //         if(fx.getMBit(1, m1))
        //             std::cout << "    M1 = " << (m1 ? "ON" : "OFF") << std::endl;
        //         uint16_t d142 = 0;
        //         if (fx.getDRegister(142, d142))
        //             std::cout << "    D142 = " << d142 << std::endl;
        //         if(fx.getSBit(0, s0))
        //             std::cout << "    S0= " << (s0 ? "ON" : "OFF") << std::endl;
        //     }
        //     std::cout << ">>> [测试] 完毕\n" << std::endl;
    }
  
    // ====================== 优雅退出 ======================
    std::cout << std::endl;
    std::cout << "=============================================" << std::endl;
    std::cout << ">>> 开始关闭服务..." << std::endl;
    
    mqttService.stop();
    logger->stop();
    
    std::cout << ">>> 服务已关闭，系统退出成功" << std::endl;
    std::cout << "=============================================" << std::endl;

   

    return 0;
}