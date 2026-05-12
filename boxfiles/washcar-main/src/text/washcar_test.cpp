#include <csignal>
#include "business_layer/lobby/lobby_service.h"
#include "presentation_layer/http_service.h"
#include "business_layer/command/mqtt_command.h"
#include "business_layer/command/mqtt/mqtt_service.h"
#include "business_layer/command/command_service.h"
#include "business_layer/washcar/washcar_service.h"
#include "common/log/log_manager.h"
#include "data_layer/fx_plc/fx_plc_device.h"
#include "data_layer/fx_plc/fx_plc_instance.h"
#include "data_layer/fx_plc/fx_plc_instance_set.h"
#include <nlohmann/json.hpp>

#include <fcntl.h>
#include <cstring>

using json = nlohmann::json;

bool g_running = true;

void signalHandler(int signum) {
    if (signum == SIGINT) {
        std::cout << ">>> 接收到 Ctrl+C，正在关闭系统..." << std::endl;
    } else if (signum == SIGTERM) {
        std::cout << ">>> 接收到终止信号，正在关闭系统..." << std::endl;
    }
    g_running = false;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    std::cout << "=============================================" << std::endl;
    std::cout << "洗车业务流程测试" << std::endl;
    std::cout << "=============================================" << std::endl;

    std::cout << "\n>>> 初始化 FX PLC..." << std::endl;
    FxPlcDevice fxDevice("fx_plc_01", "/dev/ttyUSB10", 9600, 7, true, 1);
    auto fxPlc = std::make_unique<FxPlcInstance>(fxDevice);
    FxPlcInstanceSet fxPlcSet;
    fxPlcSet.addPlc(std::move(fxPlc));

    std::unordered_map<std::string,std::unique_ptr<Camera> >  cameras;
    cameras.emplace("camera_00x",std::make_unique<Camera> ("camera_00x","camera","") );
    CameraInstanceSet cameraInstances(std::move(cameras) );

    std::vector<DoorLock> doorLocks;
    GPIODeviceInstanceSet gpioInstanceSet = GPIODeviceInstanceSet(doorLocks);

    std::vector<TempHumidSensor> sensors;
    SerialDirectDeviceInstanceSet serialInstances = SerialDirectDeviceInstanceSet(sensors);

    DeviceManageService deviceManageService(
        std::move(fxPlcSet),
        std::move(cameraInstances),
        std::move(gpioInstanceSet),
        std::move(serialInstances)
    );

    DeviceAcquisitionTask deviceAcquisitionTask(deviceManageService.getFxPlcSet());
    DeviceStatusCache deviceStatusCache = DeviceStatusCache();
    RealTimeFrameCache realTimeFrameCache = RealTimeFrameCache();

    DeviceService deviceService(deviceManageService, deviceStatusCache, deviceAcquisitionTask, realTimeFrameCache);

    std::cout << "\n>>> 初始化日志系统..." << std::endl;
    auto queue = std::make_shared<BlockingQueue>();
    auto logger = std::make_shared<AsyncLogger>(queue);
    auto fileSink = std::make_shared<FileSink>("log");
    logger->addSink(fileSink);

    if (!logger->start()) {
        std::cerr << ">>> 错误：日志系统启动失败" << std::endl;
        return -1;
    }
    LoggerManager::instance().registerLogger("work", logger);

    std::cout << "\n>>> 初始化数据库..." << std::endl;
    auto& dbManager = DatabaseManager::instance();
    if (!dbManager.init("/home/lin/Desktop/Framework/include/common/database")) {
        std::cerr << ">>> 错误：数据库初始化失败" << std::endl;
        logger->stop();
        return -1;
    }

    CommandDao commandDao(dbManager.getCommandDB());
    NetworkService* networkService = nullptr;
    SafetyService safetyService;
    CommandService commandService(networkService, commandDao);
    Timer timer;
    WashCarService washcarService(deviceService);
    LobbyService lobbyService(safetyService, commandService, deviceService, washcarService, timer, deviceStatusCache);

    std::cout << "\n>>> 等待设备采集启动..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::cout << "\n=============================================" << std::endl;
    std::cout << "测试1: 查询初始 PLC 状态" << std::endl;
    std::cout << "=============================================" << std::endl;
    {
        BoxDeviceStatus status = deviceService.viewAllDeviceStatus();
        const auto& fxList = status.getFxPlcStatusList();
        for (const auto& fx : fxList) {
            std::cout << "PLC ID: " << fx.getDeviceId() << std::endl;

            bool y52 = false, y74 = false, m381 = false, m187 = false, m1 = false;
            if (fx.getYBit(52, y52)) std::cout << "  Y52 = " << (y52 ? "ON" : "OFF") << std::endl;
            if (fx.getYBit(74, y74)) std::cout << "  Y74 = " << (y74 ? "ON" : "OFF") << std::endl;
            if (fx.getMBit(381, m381)) std::cout << "  M381 = " << (m381 ? "ON" : "OFF") << std::endl;
            if (fx.getMBit(187, m187)) std::cout << "  M187 = " << (m187 ? "ON" : "OFF") << std::endl;
            if (fx.getMBit(1, m1)) std::cout << "  M1 = " << (m1 ? "ON" : "OFF") << std::endl;

            uint16_t d142 = 0;
            if (fx.getDRegister(142, d142)) std::cout << "  D142 = " << d142 << std::endl;
        }
    }

    std::cout << "\n=============================================" << std::endl;
    std::cout << "测试2: 测试 WashCarOperation" << std::endl;
    std::cout << "=============================================" << std::endl;
    {
        json j;
        j["boxNo"] = "box001";
        j["plcId"] = "fx_plc_01";
        j["mode"] = "auto";
        j["reqSource"] = "http";
        j["time"] = "2025-01-01 12:00:00";

        WashCarOperation operation(j);
        std::cout << "boxNo: " << operation.getBoxNo() << std::endl;
        std::cout << "plcId: " << operation.getPlcId() << std::endl;
        std::cout << "mode: " << operation.getMode() << std::endl;
        std::cout << "reqSource: " << operation.getReqSource() << std::endl;
        std::cout << "isValid: " << (operation.isValid() ? "true" : "false") << std::endl;
    }

    std::cout << "\n=============================================" << std::endl;
    std::cout << "测试3: 调用 WashCarService 直接接口" << std::endl;
    std::cout << "=============================================" << std::endl;
    {
        json j;
        j["boxNo"] = "box001";
        j["plcId"] = "fx_plc_01";
        j["mode"] = "auto";
        j["reqSource"] = "http";
        j["time"] = "2025-01-01 12:00:00";

        WashCarOperation operation(j);

        //std::cout << "\n--- 调用 startWashCar ---" << std::endl;
        //auto result = washcarService.startWashCar(operation);
        //std::cout << "code: " << result.getCode() << std::endl;
        //std::cout << "message: " << result.getMessage() << std::endl;
        //std::cout << "currentStep: " << result.getCurrentStep() << std::endl;
        //std::cout << "isWashing: " << (washcarService.isWashing() ? "true" : "false") << std::endl;

        //std::cout << "\n--- 调用 getWashCarStatus ---" << std::endl;
        //auto status = washcarService.getWashCarStatus(operation);
        //std::cout << "code: " << status.getCode() << std::endl;
        //std::cout << "currentStep: " << status.getCurrentStep() << std::endl;
        //std::cout << "message: "  << status.getMessage() << std::endl;

        std::cout << "\n--- 调用 stopWashCar ---" << std::endl;
        auto stopResult = washcarService.stopWashCar(operation);
        std::cout << "code: " << stopResult.getCode() << std::endl;
        std::cout << "message: " << stopResult.getMessage() << std::endl;

        /*std::cout << "\n--- 调用 resetWashCar ---" << std::endl;
        auto resetResult = washcarService.resetWashCar(operation);
        std::cout << "code: " << resetResult.getCode() << std::endl;
        std::cout << "message: " << resetResult.getMessage() << std::endl;*/
    }

    std::cout << "\n=============================================" << std::endl;
    std::cout << "测试4: 通过 LobbyService 启动洗车（测试定时器）" << std::endl;
    std::cout << "=============================================" << std::endl;
    {
        json j;
        j["boxNo"] = "box001";
        j["plcId"] = "fx_plc_01";
        j["mode"] = "auto";
        j["reqSource"] = "mqtt";
        j["time"] = "2025-01-01 12:00:00";

        WashCarOperation operation(j);

        std::cout << "\n--- 调用 startWashCar via LobbyService ---" << std::endl;
        auto result = lobbyService.startWashCar(operation);

        if (result.success) {
            std::cout << "启动成功!" << std::endl;
            std::cout << "定时器是否运行: " << (timer.isRunningUpload() ? "是" : "否") << std::endl;

            std::cout << "\n--- 等待 5 秒观察定时器触发 ---" << std::endl;
            for (int i = 0; i < 10; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                std::cout << "  [" << (i+1)*0.5 << "s] 定时器运行中... isRunning: " 
                          << (timer.isRunningUpload() ? "是" : "否") << std::endl;
            }

            std::cout << "\n--- 调用 stopWashCar ---" << std::endl;
            auto stopResult = lobbyService.stopWashCar(operation);
            std::cout << "停止结果: " << stopResult.message << std::endl;
            std::cout << "定时器是否运行: " << (timer.isRunningUpload() ? "是" : "否") << std::endl;
        } else {
            std::cout << "启动失败: " << result.message << std::endl;
        }
    }

    std::cout << "\n=============================================" << std::endl;
    std::cout << "测试5: 测试 WashCarOperationResult 扩展字段" << std::endl;
    std::cout << "=============================================" << std::endl;
    {
        WashCarOperationResult result("box001", 0, "success");
        result.setCurrentStep(5);
        result.setFaultCode("E001");
        result.setFaultDescription("温度过高");

        std::cout << "boxNo: " << result.getBoxNo() << std::endl;
        std::cout << "code: " << result.getCode() << std::endl;
        std::cout << "message: " << result.getMessage() << std::endl;
        std::cout << "currentStep: " << result.getCurrentStep() << std::endl;
        std::cout << "faultCode: " << result.getFaultCode() << std::endl;
        std::cout << "faultDescription: " << result.getFaultDescription() << std::endl;
        std::cout << "isCompleted: " << (result.isCompleted() ? "true" : "false") << std::endl;
        std::cout << "hasFault: " << (result.hasFault() ? "true" : "false") << std::endl;
        std::cout << "isStopped: " << (result.isStopped() ? "true" : "false") << std::endl;
        std::cout << "toJson: " << result.toJson() << std::endl;
    }

    std::cout << "\n=============================================" << std::endl;
    std::cout << "所有测试完成!" << std::endl;
    std::cout << "=============================================" << std::endl;

    logger->stop();
    return 0;
}