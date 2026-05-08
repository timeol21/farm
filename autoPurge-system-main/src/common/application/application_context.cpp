#include "common/application/application_context.h"
#include <vector>

bool ApplicationContext::build(){
    //================= 基础资源 =================
    //1.日志
    // ====================== 项目日志 ======================
    auto workQueue = std::make_shared<BlockingQueue>(); 
    auto workLogger = std::make_shared<AsyncLogger>(workQueue); //项目正常运行的
    auto workFileSink = std::make_shared<FileSink>("log"); 
    workLogger->addSink(workFileSink);
    if(!workLogger->start()){
        //日志
        return false;
    }
    LoggerManager::instance().registerLogger("work",workLogger);

    // ====================== 命令日志 ======================
    auto commandQueue = std::make_shared<BlockingQueue>();
    auto commandLogger = std::make_shared<AsyncLogger>(commandQueue);
    auto commandFileSink = std::make_shared<FileSink>("command_log");
    commandLogger->addSink(commandFileSink);

    if (!commandLogger->start()) {
        std::cerr << ">>> 错误：命令日志系统启动失败，程序退出" << std::endl;
        return -1;
    }

    LoggerManager::instance().registerLogger("command_work", commandLogger);

    std::cout << ">>> 日志系统启动成功" << std::endl;


    //2.配置
    if(!SystemConfig::instance().init(CONFIGPATH)){

        return false;
    }



    // ================= 基础共享资源（跨模块共享） =================
    frame_buffer_ = std::make_shared<FrameBuffer>();              // 视频帧缓存（AI检测 / 摄像头共用）

    // ================= 基础共享资源（自身模块内共享） =================
    device_status_cache_ = std::make_shared<DeviceStatusCache>(); // 设备状态缓存

    // ================= 设备底层能力（偏基础设施层） =================
    driver_manager_ = std::make_unique<DriverManager>(); //驱动管理
    device_driver_factory_ = std::make_unique<DriverFactory>(*driver_manager_);              // 驱动工厂（创建不同厂商驱动）
    vendor_requirement_analyzer_ = std::make_unique<VendorRequirementAnalyzer>(); // 厂商差异解析（海康/大华等）
    device_access_path_resolver_ = std::make_unique<DeviceAccessPathResolver>();  // 设备访问路径解析（IP/通道等）
    sdk_environment_manager_ = std::make_unique<SdkEnvironmentManager>();    // SDK环境管理（初始化/销毁SDK）
    
    device_runtime_factory_ = std::make_unique<DeviceRuntimeFactory>();      // 运行时对象工厂（设备运行实例）
    


    // ================= 设备构建 & 存储 =================
    device_topology_builder_ = std::make_unique<DeviceTopologyBuilder>(); // 构建设备拓扑结构（配置 -> 结构）
    device_repository_ = std::make_unique<DeviceRepository>();            // 设备数据仓库（持久配置 / 元数据）

    device_runtime_builder_ = std::make_unique<DeviceRuntimeBuilder>( //构建出相应的设备实体模型
        *device_repository_,             // 1. 必须加：仓库
        *device_runtime_factory_,        // 2. 运行时工厂
        *device_access_path_resolver_,   // 3. 路径解析器
        *device_driver_factory_,         // 4. 驱动工厂
        *device_topology_builder_,       // 5. 必须加：拓扑构建器
        *vendor_requirement_analyzer_,   // 6. 厂商分析器
        *sdk_environment_manager_,       // 7. SDK管理器
        frame_buffer_                    // 8. 帧缓存  
    );


    // ================= 设备运行态管理 =================
    device_registry_ = std::make_unique<DeviceRegistry>(); // 注册所有运行中的设备实例（内存索引）
    runtime_context_ = std::make_shared<RuntimeContext>(); /// 运行设备的所需的内容
    runtime_context_->frameBuffer = frame_buffer_;


    device_bootstrapper_ = std::make_unique<DeviceBootstrapper>(
        *device_repository_,
        *device_runtime_builder_
    ); // 启动设备（根据配置创建运行时）

    device_recovery_manager_ = std::make_unique<DeviceRecoveryManager>(
        *device_registry_,
        device_status_cache_,
        *device_repository_,
        *device_runtime_builder_
    ); // 故障恢复（掉线重连 / 异常修复）

    device_poller_ = std::make_unique<DevicePoller>(
        *device_registry_,
        device_status_cache_,
        *device_recovery_manager_
    ); // 轮询设备状态（心跳 / 状态更新）

    
    device_runtime_manager_ = std::make_unique<DeviceRuntimeManager>(
        *device_bootstrapper_,
        *device_poller_,
        *device_recovery_manager_,
        *device_registry_,
        device_status_cache_,
        *driver_manager_                  
    ); // 设备运行总控（启动 + 监控 + 恢复）


    // ================= 查询能力 =================
    device_state_query_ = std::make_unique<DeviceStateQuery>(
        device_status_cache_
    ); // 对外提供设备状态查询接口

    
    unclog_dao_  = std::make_unique<UnclogDao>();

    alarm_dao_ = std::make_unique<AlarmDao>();


    // ================= 业务能力服务 =================
    device_service_ = std::make_unique<DeviceService>(
        *device_runtime_manager_,
        *device_state_query_,
        frame_buffer_
    ); // 设备服务（对外提供设备操作能力）

    
    NetworkService* networkService = nullptr;

    command_dao = std::make_unique<CommandDao>();

    
    command_service_ = std::make_unique<CommandService>(networkService,*command_dao);     // 指令封装

    safety_service_ = std::make_unique<SafetyService>();       // 安全校验（权限 / 状态合法性）

    detection_service_ = std::make_unique<DetectionService>(); // 检测服务（基于帧缓冲）


    // ================= 核心业务（用例 / 编排层） =================
    unclog_service_ = std::make_unique<UnclogService>(
        *device_service_,
        *detection_service_,
        *unclog_dao_,
        *alarm_dao_
    ); // 清堵流程编排（核心“大脑”，但只做流程控制）


    // ================= 大厅服务 =================
    lobby_service_ = std::make_unique<LobbyService>(
        *unclog_service_
    ); // 大厅服务（系统入口）

    
    
    detection_service_->registerObserver(unclog_service_.get());

    // ================= 表现层 =================


    // ================= 外部协议服务 =================
    mqtt_controller = std::make_unique<MQTTCommandController>(*lobby_service_);
    http_controller = std::make_unique<HTTPCommandController>(*lobby_service_);

    mqtt_protocol = std::make_unique<MqttProtocol>();
   

    mqtt_service = std::make_unique<MqttService>(*mqtt_controller,*mqtt_protocol);

    http_service = std::make_unique<WebService>(*http_controller);

    command_service_->immitDependence(*mqtt_service); 

    return true;
}


void ApplicationContext::start(){

  
   
  
    
    
}

void ApplicationContext::stop(){
    
}


void ApplicationContext::destroy(){
    
   
}

