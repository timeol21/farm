#include <csignal>
#include "business_layer/lobby/lobby_service.h"
#include "presentation_layer/http_service.h"
#include "business_layer/command/mqtt_command.h"
#include "business_layer/command/mqtt/mqtt_service.h"
#include "business_layer/command/command_service.h"
#include "business_layer/detection/detection.h"
#include "common/log/log_manager.h"
#include "data_layer/fx_plc/fx_plc_device.h"
#include "data_layer/fx_plc/fx_plc_instance.h"
#include "data_layer/fx_plc/fx_plc_instance_set.h"
#include "business_layer/washcar/washcar_service.h"
//#include "business_layer/device/device_change_listener.h"

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
// class TestChangeListener : public IDeviceChangeListener {
// public:
//     void onDeviceChanged(const ChangeRecord& change) override {
//         std::cout << "[变化检测] 设备: " << change.deviceId
//                   << "  点位: " << change.pointName
//                   << "  旧值: " << change.oldValue
//                   << "  新值: " << change.newValue
//                   << "  类型: " << change.deviceType
//                   << std::endl;
//     }
// };
// 在 main() 之前添加一个辅助函数，方便打印 M 点状态
void printMStates(IPlcInstance* plc, int start, int end) {
    std::cout << ">>> [实时读取] M" << start << "~" << end << " 状态:" << std::endl;
    for (int m = start; m <= end; ++m) {
        bool state = false;
        if (plc->readM(m, state)) {
            std::cout << "M" << m << " = " << (state ? "ON" : "OFF") << std::endl;
        } else {
            std::cerr << "M" << m << " 读取失败!" << std::endl;
        }
    }
}


int main(int argc, char* argv[]) {
    // 注册信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    std::cout << "=============================================" << std::endl;
    std::cout << "正在启动项目中....." << std::endl;
    std::cout << "\n>>> 开始与 FX PLC 通讯..." << std::endl;
    FxPlcDevice fxDevice("fx_plc_01", "/dev/ttyUSB10", 9600, 7, true, 1);
    auto fxPlc = std::make_unique<FxPlcInstance>(fxDevice);
    FxPlcInstanceSet fxPlcSet;
    fxPlcSet.addPlc(std::move(fxPlc));
   
 
    IPlcInstance* fxPlcRaw = fxPlcSet.getPlc("fx_plc_01");
    if (!fxPlcRaw) {
        std::cerr << "无法获取 PLC 实例指针" << std::endl;
        return -1;
    }
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
    
    // std::unordered_map<std::string, std::unique_ptr<Camera>> cameras;
    // CameraDevice cameraDevice("camera_001", "实验室摄像头", "rtsp://krdn:ph4ctc@192.168.31.208:554/Streaming/Channels/101");
    // cameras.emplace("camera_001", std::make_unique<Camera>(cameraDevice));
    // CameraInstanceSet cameraInstances(std::move(cameras));
    std::unordered_map<std::string, std::unique_ptr<Camera>> emptyCameras;
    CameraInstanceSet cameraInstances(std::move(emptyCameras));

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
        DeviceAcquisitionTask deviceAcquisitionTask(deviceManageService.getFxPlcSet(), "haiTian");
    DeviceStatusCache deviceStatusCache = DeviceStatusCache();

    //监听测试
    //auto testListener = std::make_shared<TestChangeListener>();
    //deviceStatusCache.addListener(testListener);


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
    // auto& dbManager = DatabaseManager::instance();
    // if (!dbManager.init("/home/lin/Desktop/Framework/include/common/database")) {
    //     std::cerr << ">>> 错误：数据库初始化失败，程序退出" << std::endl;
    //     logger->stop();  // 清理已启动的日志
    //     return -1;
    // }
    std::cout << ">>> 数据库初始化成功" << std::endl;
    auto& dbManager = DatabaseManager::instance();
    /*if (!dbManager.init("../data")) {
        std::cerr << ">>> 错误：数据库初始化失败" << std::endl;
        return -1;
    }*/

    CommandDao commandDao(dbManager.getCommandDB());
    NetworkService* networkService = nullptr;
    SafetyService safetyService;
    CommandService commandService(networkService, commandDao);
    HaiTianWashCarService washcarService(deviceService);
    Timer timer;
    
    ModuleDao moduleDao(dbManager);
    DetectionResultCache detectionResultCache;
    DetectionService detectionService(moduleDao, realTimeFrameCache, detectionResultCache, washcar::cfg::AiConfig{});
    //LobbyService lobbyService(safetyService, commandService, deviceService, washcarService, timer, deviceStatusCache, detectionService);

    // MQTTCommandController mqttController = MQTTCommandController(lobbyService);
    // HTTPCommandController httpController = HTTPCommandController(lobbyService);

    // MqttProtocol mqttProtocol;
    // MqttService mqttService("192.168.31.254", 1883, "admin", "ac123",mqttController, mqttProtocol);
    // commandService.immitDependence(mqttService); 


    // ====================== MQTT 启动 + 失败处理（核心需求） ======================
    // std::cout << ">>> 正在启动 MQTT 服务..." << std::endl;
    // if (!mqttService.start()) {
    //     std::cerr << ">>> 错误：MQTT 服务启动失败，程序退出" << std::endl;
    //     // 只清理已经启动的资源，不调用 stop()
    //     logger->stop();
    //     return -1;
    // }
    // std::cout << ">>> MQTT 服务启动成功" << std::endl;
    
    // ====================== 所有服务启动完成 ======================
    std::cout << std::endl;
    std::cout << "=============================================" << std::endl;
    std::cout << ">>> 系统启动成功！服务已就绪 (进程 ID: " << getpid() << ")" << std::endl; 
    std::cout << ">>> 按 Ctrl+C 停止服务" << std::endl;
    std::cout << "=============================================" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::this_thread::sleep_for(std::chrono::seconds(10));
     std::cout << ">>> 系统启动成功..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // ========== 1. 置位 M600 ~ M616 ==========
    // std::cout << "\n>>> 开始置位 M600 ~ M616 ..." << std::endl;
    // for (int m = 600; m <= 616; ++m) {
    //     if (deviceManageService.forceM("fx_plc_01", m, true)) {
    //         std::cout << "置位 M" << m << " 成功" << std::endl;
    //     } else {
    //         std::cerr << "置位 M" << m << " 失败" << std::endl;
    //     }
    //     std::this_thread::sleep_for(std::chrono::milliseconds(50));
    // }
    // std::this_thread::sleep_for(std::chrono::seconds(1));

    // ========== 2. 实时读取 M600~M616 ==========
    //printMStates(fxPlcRaw, 100,101);   
    // std::cout << "\n>>> 开始置位 X74 ..." << std::endl;
    // if (fxPlcRaw->forceX(60, true)) {
    //     std::cout << "置位 X74 成功" << std::endl;
    // } else {
    //     std::cerr << "置位 X74 失败" << std::endl;
    // }

    // 等待缓存更新（采集任务会每 500ms 更新一次）
    // std::this_thread::sleep_for(std::chrono::seconds(1));

    // printCachedMStates(deviceStatusCache, "fx_plc_01", 600, 616);


    // 主循环 — 跑 N 轮后退出,保证 target 形态(binary 不会永远 hang)
    // N 调成 3 是因为 PLC 测试每轮 5s,3 轮 ≈ 15s 就够看 s0/m187 的状态变化
    const int kMaxIterations = 3;
    int iter = 0;
    while (g_running && iter < kMaxIterations) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

       std::this_thread::sleep_for(std::chrono::milliseconds(500));
        //std::cout << "\n>>> [测试] 缓存读取" << std::endl;

            BoxDeviceStatus directStatus = deviceService.viewAllDeviceStatus();

            const auto& fxList = directStatus.getFxPlcStatusList();
            //std::cout << ">>> [测试] FX PLC 数量: " << fxList.size() << std::endl;

            for (const auto& fx : fxList) {
                //std::cout << "  PLC ID: " << fx.getDeviceId() << std::endl;
                // uint16_t d150 = 0;
                //     if (fx.getDRegister(150, d150)) {
                //         std::cout << "D150 = " << d150 << std::endl;
                //     } else {
                //         std::cerr << "D150 读取失败!" << std::endl;
                //     }
                bool s0=0;
                    if(fx.getSBit(0,s0)){
                        std::cout<<"s0="<<s0<<std::endl;
                    }else{
                        std::cerr<<"err"<<std::endl;
                    }
                bool m187 = 0;
                    if (fx.getMBit(187, m187)) {
                        std::cout << "m187=" << m187 << std::endl;
                    }
                    else {
                        std::cerr << "err" << std::endl;
                    }
                //bool y50 = false, y51 = false, y74 = false,x23=false,m1=false,s0=false;
                // bool y77=false;
                // if (fx.getYBit(77, y77))
                //     std::cout << " y77 = " << (y77 ? "ON" : "OFF") << std::endl;
                //  for (int i = 600; i <= 616; i++) {
                //     bool state = false;
                //     if (fx.getMBit(i, state)) {
                //         // 格式化输出，让结果更整齐
                //         std::cout << "M" << i << " = " << (state ? "ON " : "OFF") << std::endl;
                //     } else {
                //         std::cerr << "M" << i << " 读取失败!" << std::endl;
                //     }
                // }
            }
            std::cout << ">>> [测试] 完毕\n" << std::endl;

            std::this_thread::sleep_for(std::chrono::milliseconds(5000));
            iter++;
    }
  
    // ====================== 优雅退出 ======================
    std::cout << std::endl;
    std::cout << "=============================================" << std::endl;
    std::cout << ">>> 开始关闭服务..." << std::endl;
    
    //mqttService.stop();
    logger->stop();
    
    std::cout << ">>> 服务已关闭，系统退出成功" << std::endl;
    std::cout << "=============================================" << std::endl;

   

    return 0;
}
