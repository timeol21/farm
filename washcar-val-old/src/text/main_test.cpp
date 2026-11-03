#include <iostream>
#include <csignal>
#include <memory>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <filesystem>
#include <system_error>
#include <cstdlib>
#include "business_layer/lobby/lobby_service.h"
#include "presentation_layer/http_service.h"
#include "presentation_layer/http_command.h"
#include "business_layer/command/mqtt_command.h"
#include "business_layer/command/mqtt/mqtt_service.h"
#include "business_layer/command/mqtt/protocol.h"
#include "business_layer/command/command_service.h"
#include "business_layer/washcar/washcar_service.h"
#include "business_layer/device/device_service.h"
#include "business_layer/device/device_manage_service.h"
#include "business_layer/detection/detection.h"
#include "data_layer/camera/camera_device.h"
#include "business_layer/device/device_acquisition_task.h"
#include "business_layer/device/device_status_cache.h"
#include "business_layer/timer.h"
#include "common/log/log.h"        // 定义 ILogger
#include "common/log/log_manager.h"  // LoggerManager
#include "common/log/log_sink.h"   // FileSink / ConsoleSink
//#include "common/database/database_manager.h"
//#include "common/database/command_dao.h"
#include "common/config/config_manager.h"
#include "data_layer/fx_plc/fx_plc_device.h"
#include "data_layer/fx_plc/fx_plc_instance.h"
#include "data_layer/fx_plc/tcp_plc_instance.h"
#include "data_layer/fx_plc/fx_plc_instance_set.h"
#include "data_layer/camera/camera.h"
#include "data_layer/camera/camera_instance_set.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

bool g_running = true;

void signalHandler(int signum) {
    if (signum == SIGINT) {
        std::cout << "\n>>> 接收到 Ctrl+C，正在关闭系统..." << std::endl;
    } else if (signum == SIGTERM) {
        std::cout << "\n>>> 接收到终止信号，正在关闭系统..." << std::endl;
    }
    g_running = false;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    std::cout << "=============================================" << std::endl;
    std::cout << "洗车机集成测试启动" << std::endl;
    std::cout << "=============================================" << std::endl;

    // ============ 1) 算 exe 目录 + chdir(spec §16.2 / §16.3) ============
    // 配置文件 / 日志 / 数据库统一在 exe 同级目录,不受 systemd WorkingDirectory 影响
    std::filesystem::path exeDir;
    try {
        exeDir = std::filesystem::absolute(argv[0] ? argv[0] : "").parent_path();
    } catch (...) {
        exeDir = std::filesystem::current_path();   // §16.6 兜底
    }
    if (exeDir.empty()) {
        exeDir = std::filesystem::current_path();
    }
    std::filesystem::current_path(exeDir);

    const char* configuredPath = std::getenv("WASHCAR_CONFIG");
    const std::string configPath = configuredPath && *configuredPath
        ? std::string(configuredPath)
        : (exeDir / "config.json").string();

    {
        std::string err;
        if (!ConfigManager::instance().load(configPath, err)) {
            std::cerr << ">>> 致命: 配置加载失败: " << err << std::endl;
            return -1;
        }
    }
    const auto& cfg = ConfigManager::instance();

    // ============ 命令行覆盖(本期已去掉) ============
    // 原 main_test.cpp 用 argv[1..5] 覆盖 MQTT IP/port/boxNo/username/password,
    // 本期改为 JSON 配置驱动;现场调参改 /etc/washcar/config.json(或本地 config.json)即可。
    // 若未来需要命令行调试入口,在 ConfigManager 加 mutable override 句柄。

    std::cout << "\n>>> 初始化日志系统..." << std::endl;
    auto queue = std::make_shared<BlockingQueue>();
    auto logger = std::make_shared<AsyncLogger>(queue);
    auto resolvePath = [&exeDir](const std::string& value, const std::string& fallback) {
        std::filesystem::path p = value.empty() ? std::filesystem::path(fallback) : std::filesystem::path(value);
        return p.is_absolute() ? p.string() : (exeDir / p).lexically_normal().string();
    };
    const std::string storageRoot = resolvePath(cfg.storageDir(), "./data");
    // 库和标志文件平铺在 storageRoot 下,只有图片/日志这种一堆文件的才开子目录
    const std::string logPath = resolvePath(cfg.logDir(), storageRoot + "/log");
    const std::string imagesPath = resolvePath(cfg.imagesDir(), storageRoot + "/images");
    std::filesystem::create_directories(storageRoot);
    if (!imagesPath.empty()) std::filesystem::create_directories(imagesPath);
    auto fileSink = std::make_shared<FileSink>(logPath);   // 日志目录由 FileSink 自己建
    auto consoleSink = std::make_shared<ConsoleSink>();  // 现场调试：LOG_* 同时输出到终端
    logger->addSink(fileSink);
    logger->addSink(consoleSink);
    // 日志级别可配: 默认 info。DEBUG 里全是心跳(fault monitor tick 每 3 秒一条、
    // MQTT 每收发一包一条), 落盘会刷屏; 现场要排查时把 config.json 的
    // box.logLevel 改成 "debug" 重启即可。
    {
        const std::string lvl = cfg.logLevel();
        LogLevel level = LogLevel::INFO;
        if (lvl == "debug") level = LogLevel::DEBUG;
        else if (lvl == "warning" || lvl == "warn") level = LogLevel::WARNING;
        else if (lvl == "error") level = LogLevel::ERROR;
        logger->setLogLevel(level);
        std::cout << ">>> 日志级别: " << lvl << std::endl;
    }
    if (!logger->start()) {
        std::cerr << ">>> 错误：日志系统启动失败" << std::endl;
        return -1;
    }
    LoggerManager::instance().registerLogger("work", logger);
    // 配置 dump 放在 logger 注册之后: 早于这里调用时日志系统还没起来,LOG_* 会被丢掉。
    ConfigManager::instance().dumpToLog();

    std::cout << "\n>>> 初始化数据层..." << std::endl;
    // 盒子只连一个 PLC(海天串口 或 驿公里 Modbus TCP),互斥注入;一个盒子跑两种协议会连接冲突
    const std::string boxType = cfg.box().boxType;
    FxPlcInstanceSet fxPlcSet;
    if (!cfg.box().plcEnabled) {
        std::cout << "  - PLC 已关闭（开发者测试模式，不注册 PLC）" << std::endl;
    } else if (boxType == "yiGongLi") {
        // 驿公里 Modbus TCP(对称海天 cfg.plcSerial 走 FX 串口)
        const auto& tcp = cfg.plcTcp();
        fxPlcSet.addPlc(std::make_unique<TcpPlcInstance>(
            tcp.plcId, tcp.host, static_cast<uint16_t>(tcp.port), static_cast<uint8_t>(tcp.unitId)));
        std::cout << "  - boxType=yiGongLi, 注入 " << tcp.plcId
                  << " (Modbus TCP " << tcp.host << ":" << tcp.port << ")" << std::endl;
    } else {
        // 海天串口(原 main_test.cpp:107-114 风格,直接从 ConfigManager 读)
        const auto& plcSer = cfg.plcSerial();
        FxPlcDevice fxDevice("fx_plc_01",
            plcSer.port,
            plcSer.baudRate,
            plcSer.dataBits,
            plcSer.parity == "even",
            plcSer.stopBits);
        fxPlcSet.addPlc(std::make_unique<FxPlcInstance>(fxDevice));
        std::cout << "  - boxType=haiTian(默认), 注入 fx_plc_01 (海天串口)" << std::endl;
    }

    // 摄像头从 ConfigManager 读(原硬编码一个实验室摄像头;现按 cfg.cameras() 全量构造)
    // [FIX-01] 帧缓冲必须提前声明, 并在摄像头创建后立即注入:
    //   Camera::pullKeyFrameLoop 只通过 frameSink_ 往外写帧, 若没有调用过 setFrameSink
    //   (frameSink_ 恒为 nullptr), 帧缓冲就永远为空、检测服务拿不到帧。此处补上注入。
    //   注意注入顺序: 先 setFrameSink 再 start, 否则 start 之后解出的前几帧会被丢弃。
    RealTimeFrameCache realTimeFrameCache;
    std::unordered_map<std::string, std::unique_ptr<Camera>> cameras;
    for (const auto& cam : cfg.cameras()) {
        std::cout << "  - camera[" << cam.id << "]: " << cam.url << std::endl;
        CameraDevice cameraDev(cam.id, cam.name, cam.url);
        auto camPtr = std::make_unique<Camera>(cameraDev);
        camPtr->setFrameSink(&realTimeFrameCache);   // ① 先注入帧接收方
        camPtr->start();                             // ② 再拉起拉流线程（顺序不能反）
        cameras.emplace(cam.id, std::move(camPtr));
    }
    CameraInstanceSet cameraInstances(std::move(cameras));

    DeviceManageService deviceManageService(
        std::move(fxPlcSet),
        std::move(cameraInstances)
    );

    std::cout << "\n>>> 初始化设备服务层..." << std::endl;
    // 采集任务按厂商选协议,并拿到这台洗车机的软元件表(采哪些点由表决定)
    DeviceAcquisitionTask deviceAcquisitionTask(deviceManageService.getFxPlcSet(), cfg.machine());
    DeviceStatusCache deviceStatusCache;
    DeviceService deviceService(deviceManageService, deviceStatusCache, deviceAcquisitionTask, realTimeFrameCache);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    // ============ 业务层(海天/驿公里二选一,互斥,不共存) ============
    std::cout << "\n>>> 初始化业务层..." << std::endl;
    SafetyService safetyService;
    Timer timer;

    auto& dbManager = DatabaseManager::instance();
    std::error_code ec;
    std::filesystem::create_directories(storageRoot, ec);
    if (!dbManager.init(storageRoot)) {
        std::cerr << ">>> 错误：数据库初始化失败, path=" << storageRoot << std::endl;
        return -1;
    }
    CommandDao commandDao(dbManager.getCommandDB());
    // MQTT outbox 必须接真实库:MessageBuffer 不注入 db_ 时 saveToDatabase 直接 return,
    // 断网期间的上报会被静默丢弃(连日志都没有,logger_ 同样为空),重连补发也就无从谈起。
    MessageBuffer::getInstance().setDatabase(dbManager.getCommandDB());
    MessageBuffer::getInstance().setLogger([](const std::string& msg) { LOG_INFO("[MessageBuffer] " + msg); });
    NetworkService* networkService = nullptr;
    CommandService commandService(networkService, commandDao);

    ModuleDao moduleDao(dbManager);
    DetectionResultCache detectionResultCache;
    // 检测服务: 区域-模型对、模型定义、检测频率全部从配置文件(ConfigManager::instance().ai())读取;
    // 构造内加载全部模型(任一失败抛异常, 进程退出); cfg.ai().modelConfigs 空 = 不接 AI 检测。
    DetectionService detectionService(moduleDao, realTimeFrameCache, detectionResultCache, cfg.ai());
    detectionService.startDetection();

    std::cout << "\n>>> 初始化通信层..." << std::endl;

    MqttProtocol protocol;
    std::unique_ptr<MqttService> mqttService;
    std::thread mqttThread;
    const auto& mqttCfg = cfg.mqtt();

    if (boxType == "yiGongLi") {
        // ============== 驿公里 ==============
        // 洗车服务栈对象(对称海天原本风格,不用智能指针)
        std::cout << "  - 洗车机类型: YiGongLiWashCarService(驿公里 Modbus TCP)" << std::endl;
        YiGongLiWashCarService washcarService(deviceService);

        // 大厅服务
        LobbyService lobbyService(safetyService, commandService, deviceService, washcarService, timer, deviceStatusCache, detectionService);
        MQTTCommandController mqttController(lobbyService);
        HTTPCommandController httpController(lobbyService);

        if (!mqttCfg.brokerIp.empty()) {
            std::cout << "  - MQTT: " << mqttCfg.brokerIp << ":" << mqttCfg.port << std::endl;
            mqttService = std::make_unique<MqttService>(
                mqttCfg.brokerIp,
                mqttCfg.port,
                mqttCfg.username,
                mqttCfg.password,
                cfg.box().boxNo,
                mqttController,
                protocol
            );
            commandService.immitDependence(*mqttService);
            mqttThread = std::thread([&mqttService]() {
                mqttService->start();
            });
        } else {
            std::cout << "  - MQTT: 未配置 (跳过)" << std::endl;
        }

        std::cout << "\n>>> 系统启动完成!" << std::endl;
        if (!mqttCfg.brokerIp.empty()) {
            std::cout << "  - MQTT 服务: " << mqttCfg.brokerIp << ":" << mqttCfg.port << std::endl;
        }
        std::cout << "  - HTTP: 0.0.0.0:8080" << std::endl;
        WebService webService("0.0.0.0", 8080, httpController);
        if (!webService.start()) {
            std::cerr << ">>> 错误:HTTP 服务启动失败" << std::endl;
            // 主动收尾后台线程,避免 early return 触发 std::terminate
            detectionService.stopDetection();
            timer.stopUpload();
            return -1;
        }

        std::cout << "  - 盒号: " << cfg.box().boxNo << std::endl;
        std::cout << "\n按 Ctrl+C 停止系统" << std::endl;
        std::cout << "=============================================" << std::endl;

        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        std::cout << "\n>>> 关闭系统..." << std::endl;
        // 顺序要紧:大厅的各路监控跑在 timer 线程里,回调会经 CommandService 打到 MqttService。
        // MqttService 是栈外 unique_ptr,析构比 timer 早;不先停 timer 的话,
        // 这段时间里再来一次 tick 就是对着已析构对象调虚函数 -> pure virtual method called(2026-10-19 盒子实测)。
        timer.stopUpload();
        detectionService.stopDetection();
        if (mqttService) {
            mqttService->stop();
            if (mqttThread.joinable()) {
                mqttThread.join();
            }
        }
        std::cout << ">>> 系统已关闭" << std::endl;
    } else {
        // ============== 海天(默认,完全保持原 main_test 风格) ==============
        // 洗车服务栈对象(海天原本 main_test.cpp:150 就是栈对象,无智能指针)
        std::cout << "  - 洗车机类型: HaiTianWashCarService(海天串口,默认)" << std::endl;
        HaiTianWashCarService washcarService(deviceService);

        // 大厅服务
        LobbyService lobbyService(safetyService, commandService, deviceService, washcarService, timer, deviceStatusCache, detectionService);
        MQTTCommandController mqttController(lobbyService);
        HTTPCommandController httpController(lobbyService);

        if (!mqttCfg.brokerIp.empty()) {
            std::cout << "  - MQTT: " << mqttCfg.brokerIp << ":" << mqttCfg.port << std::endl;
            mqttService = std::make_unique<MqttService>(
                mqttCfg.brokerIp,
                mqttCfg.port,
                mqttCfg.username,
                mqttCfg.password,
                cfg.box().boxNo,
                mqttController,
                protocol
            );
            commandService.immitDependence(*mqttService);
            mqttThread = std::thread([&mqttService]() {
                mqttService->start();
            });
        } else {
            std::cout << "  - MQTT: 未配置 (跳过)" << std::endl;
        }

        std::cout << "  - HTTP: 0.0.0.0:8080" << std::endl;
        WebService webService("0.0.0.0", 8080, httpController);
        if (!webService.start()) {
            std::cerr << ">>> 错误：HTTP 服务启动失败" << std::endl;
            // 主动收尾后台线程，避免 early return 触发 std::terminate
            detectionService.stopDetection();
            timer.stopUpload();
            return -1;
        }

        std::cout << "\n>>> 系统启动完成!" << std::endl;
        std::cout << "  - HTTP 服务: http://localhost:8080" << std::endl;
        if (!mqttCfg.brokerIp.empty()) {
            std::cout << "  - MQTT 服务: " << mqttCfg.brokerIp << ":" << mqttCfg.port << std::endl;
        }
        std::cout << "  - 盒号: " << cfg.box().boxNo << std::endl;
        std::cout << "\n按 Ctrl+C 停止系统" << std::endl;
        std::cout << "=============================================" << std::endl;

        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        std::cout << "\n>>> 关闭系统..." << std::endl;

        //webService.stop();

        // 同上:先停 timer 线程,再析构 MqttService,否则关停时会 pure virtual method called
        timer.stopUpload();
        detectionService.stopDetection();

        if (mqttService) {
            mqttService->stop();
            if (mqttThread.joinable()) {
                mqttThread.join();
            }
        }

        //LoggerManager::instance().getLogger("work")->stop();

        std::cout << ">>> 系统已关闭" << std::endl;
    }

    return 0;
}
