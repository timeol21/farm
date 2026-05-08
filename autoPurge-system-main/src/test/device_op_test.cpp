
#include "business_layer/device/device_runtime_manager.h"
#include "common/config/config_load.h"
#include "common/log/log_manager.h"
#include <csignal>

const std::string CONFIGPATH = "/home/ztl/workspace/autoPurge-system/include/common/config/system_config.json";
#include <atomic>
std::atomic<bool> g_exit_signal {false};
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
int main(int argc, char* argv[]){
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    //1.日志
    // ====================== 项目日志 ======================
    auto workQueue = std::make_shared<BlockingQueue>(); 
    auto workLogger = std::make_shared<AsyncLogger>(workQueue); //项目正常运行的
    auto workFileSink = std::make_shared<FileSink>("log"); 
    workLogger->addSink(workFileSink);
    if(!workLogger->start()){
        std::cerr << ">>> 错误：命令日志系统启动失败，程序退出" << std::endl;
        return -1;
    }
    LoggerManager::instance().registerLogger("work",workLogger);
    std::cout << "[INFO] 日志系统初始化完成" << std::endl;

    LOG_INFO("开始项目");


    //2.配置
    if(!SystemConfig::instance().init(CONFIGPATH)){
        std::cout << "[ERROR] 系统配置初始化失败！" << std::endl;
        return 1;
    }

    DeviceRepository repository;
    DeviceRuntimeFactory runtimeFactory;
    DeviceAccessPathResolver accessResolver;
    DriverManager driverManager;
    DriverFactory driverFactory(driverManager);
    DeviceTopologyBuilder topologyBuilder;
    VendorRequirementAnalyzer vendorAnalyzer;
    SdkEnvironmentManager sdkManager;
    auto frameBuffer = std::make_shared<FrameBuffer>();

    // ====================== 核心：runtimeBuilder 构建 ======================
   
    DeviceRuntimeBuilder runtimeBuilder(
        repository, runtimeFactory, accessResolver,
        driverFactory, topologyBuilder, vendorAnalyzer,
        sdkManager, frameBuffer
    );
    
    // ======================================================================

    auto statusCache = std::make_shared<DeviceStatusCache>();
    DeviceRegistry registry;
    DeviceBootstrapper bootstrapper(repository, runtimeBuilder);
    DeviceRecoveryManager recoveryManager(registry, statusCache, repository, runtimeBuilder);
    DevicePoller poller(registry, statusCache, recoveryManager);

   
    DeviceRuntimeManager runtimeManager(bootstrapper, poller, recoveryManager, registry, statusCache,driverManager);
    

    std::cout << "\n=== 初始化所有设备 ===" << std::endl;
    if(!runtimeManager.initializeAll()){
        std::cout << "[SUCCESS] 所有设备初始化失败！" << std::endl;
    }
    // std::cout << "[SUCCESS] 所有设备初始化完成！" << std::endl;

    // std::cout << "=== 启动所有设备 ===" << std::endl;
    // runtimeManager.startAll();
    // 主循环
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    workLogger->stop();
    
    LOG_INFO("结束项目");
}