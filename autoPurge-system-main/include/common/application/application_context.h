#pragma once

#include "business_layer/lobby/lobby_service.h"
#include "business_layer/unclog/unclog_service.h"

#include "common/config/config_load.h"
#include "common/log/log_manager.h"

#include "presentation_layer/http_service.h"
#include "business_layer/command/mqtt/network_service.h"
#include "business_layer/command/mqtt/protocol.h"
#include "business_layer/command/mqtt/mqtt_service.h"
#include "business_layer/command/mqtt_command.h"

const std::string CONFIGPATH = "/home/ztl/workspace/autoPurge-system/include/common/config/system_config.json";

class ApplicationContext {
public:
    bool build();

    void start();

    void stop();
    
    void destroy();
private:
    // ===== 基础资源 =====
    std::shared_ptr<FrameBuffer> frame_buffer_;
    std::shared_ptr<DeviceStatusCache > device_status_cache_;
    

    // ===== 协议服务 ===== 
    std::unique_ptr<MqttService> mqtt_service;
    std::unique_ptr<WebService> http_service;

    // ===== 协议 ===== 
    std::unique_ptr<MqttProtocol> mqtt_protocol;

    // ===== 表示层 ===== 
    std::unique_ptr<MQTTCommandController> mqtt_controller;
    std::unique_ptr<HTTPCommandController> http_controller;


    // ===== 为Device 持久(数据层)模块提供技术支撑 ===== 
    std::unique_ptr<DriverFactory> device_driver_factory_;
    std::unique_ptr<VendorRequirementAnalyzer> vendor_requirement_analyzer_;
    std::unique_ptr<DeviceAccessPathResolver> device_access_path_resolver_;
    std::unique_ptr<SdkEnvironmentManager> sdk_environment_manager_;
    std::unique_ptr<DeviceRuntimeFactory> device_runtime_factory_;
    std::shared_ptr<RuntimeContext> runtime_context_;
    std::unique_ptr<DriverManager> driver_manager_;


    // ===== Device 持久层 ===== 去抽象创造出device 实体类（DeviceRuntime） 去进行相应持久化操作
    std::unique_ptr<DeviceTopologyBuilder> device_topology_builder_;
    std::unique_ptr<DeviceRepository> device_repository_;
    std::unique_ptr<DeviceRuntimeBuilder> device_runtime_builder_;

    //===== Unclog 持久层 =====
    std::unique_ptr<UnclogDao> unclog_dao_;

    //===== 通用持久层 =====
    std::unique_ptr<AlarmDao> alarm_dao_;

    // ===== 为Device业务模块提供技术支撑 ===== 
    std::unique_ptr<DeviceRegistry> device_registry_;
    std::unique_ptr<DevicePoller> device_poller_;
    std::unique_ptr<DeviceRecoveryManager> device_recovery_manager_;
    std::unique_ptr<DeviceBootstrapper> device_bootstrapper_;

    // ===== 为Unclog业务模块提供技术支撑 ===== 


    // ===== Device模块业务层 ===== 
    std::unique_ptr<DeviceStateQuery> device_state_query_; 
    std::unique_ptr<DeviceRuntimeManager> device_runtime_manager_; 

    std::unique_ptr<CommandDao>  command_dao;   


    // ===== 业务模块 =====
    std::unique_ptr<DeviceService> device_service_;
    std::unique_ptr<LobbyService> lobby_service_;
    std::unique_ptr<CommandService> command_service_;
    std::unique_ptr<SafetyService> safety_service_;
    std::unique_ptr<DetectionService> detection_service_;
    std::unique_ptr<UnclogService> unclog_service_;

};