
#ifndef MQTT_TOPICS_H  // 头文件保护：防止重复包含导致编译错误
#define MQTT_TOPICS_H

// 替换const std::string为宏定义（字符串宏需用双引号包裹）


// ================= 海天洗车机 MQTT 主题 =================
// 盒子订阅：接收云端命令
// 说明：早期 /device/* 风格的 legacy 上行/下行主题（电磁阀、配置更新、设备状态、
// 图像、历史视频、AI 报警、传感器报警）已全部删除。盒子只使用 flexem/{boxNo}/* 一套。
#define FLEXEM_MONITOR_TOPIC                   "monitor"
#define FLEXEM_READDATA_TOPIC                  "readdata"
#define FLEXEM_READ_TOPIC                      "read"
#define FLEXEM_PUSHALARM_TOPIC                 "pushalarm"
#define FLEXEM_COMMAND_TOPIC                   "command"
#define FLEXEM_COMMAND_REPLY_TOPIC             "commandreply"
#define FLEXEM_REBOOT_TOPIC                    "reboot"

#define WASHCAR_FUNCTION_GET_TOPIC             FLEXEM_COMMAND_TOPIC

// 盒子发布：命令执行结果、状态变化
#define WASHCAR_PROPERTY_POST_TOPIC            FLEXEM_MONITOR_TOPIC

// 盒子发布：故障/报警上报
#define WASHCAR_EVENT_POST_TOPIC               FLEXEM_PUSHALARM_TOPIC

// 盒子发布：读取数据上报
#define WASHCAR_INFO_POST_TOPIC                FLEXEM_READDATA_TOPIC

// 盒子发布：定时上报(发泡剂/蜡水)
#define WASHCAR_MONITOR_POST_TOPIC             FLEXEM_MONITOR_TOPIC

// ================= 海天洗车机 HTTP 主题 =================
#define WASHCAR_HTTP_START_TOPIC               "/%s/%s/washcar/start"
#define WASHCAR_HTTP_STOP_TOPIC                "/%s/%s/washcar/stop"
#define WASHCAR_HTTP_RESET_TOPIC               "/%s/%s/washcar/reset"
#define WASHCAR_HTTP_STATUS_TOPIC              "/%s/%s/washcar/status"
#define WASHCAR_HTTP_READ_DATA_TOPIC           "/%s/%s/washcar/readData"
#define WASHCAR_HTTP_CHANGE_MODE_TOPIC         "/%s/%s/washcar/changemode"
#define WASHCAR_HTTP_CLEAR_ALARM_TOPIC         "/%s/%s/washcar/clearAlarm"

// ================= 海天洗车机点操作 HTTP 主题 =================
#define WASHCAR_HTTP_POINT_ON_PATH             "/point/on"
#define WASHCAR_HTTP_POINT_OFF_PATH            "/point/off"
#define WASHCAR_HTTP_POINT_RESET_PATH          "/point/reset"
#define WASHCAR_HTTP_POINT_STATUS_PATH         "/point/status"
#define WASHCAR_HTTP_ALARM_CLEAR_PATH          "/alarm/clear"

// ================= 海天洗车机上报 Topic =================
#define WASHCAR_POINT_POST_TOPIC               FLEXEM_COMMAND_REPLY_TOPIC
#define WASHCAR_START_REPORT_TOPIC             FLEXEM_COMMAND_REPLY_TOPIC
#define WASHCAR_STOP_REPORT_TOPIC              FLEXEM_COMMAND_REPLY_TOPIC
#define WASHCAR_PROGRESS_REPORT_TOPIC          FLEXEM_MONITOR_TOPIC
#define WASHCAR_DAILY_REPORT_TOPIC             FLEXEM_PUSHALARM_TOPIC
#define WASHCAR_CHANGE_REPORT_TOPIC            FLEXEM_MONITOR_TOPIC
#define WASHCAR_FAULT_REPORT_TOPIC             FLEXEM_PUSHALARM_TOPIC

// ================= 驿公里洗车机 MQTT 主题 =================
// 驿公里沿用 Flexem 主题,新增别名便于代码可读(底层宏全部指向 FLEXEM_*)
// 协议: /flexem/{boxId}/{endpoint}  payload 平铺 JSON, plcId 固定 tcp_plc_01
// 端点(endpoint): command / commandreply / point/post / monitor / pushalarm
// 参考: docs/驿公里_MQTT_协议对接总结.md

// 海天/驿公里主题宏分别定义(不共用):海天走 FLEXEM_* 宏,驿公里走 YIGONGLI_* 宏
// (注:端点字符串值当前都是 "command"/"commandreply" 等,海天/驿公里共用同一组 endpoint 名;
//  后续云端协议若需独立 endpoint 名字,只改宏值即可,mqtt_command.cpp 内部 if-else 路由自动生效)

// 盒子订阅: 接收云端命令
#define YIGONGLI_COMMAND_TOPIC                 FLEXEM_COMMAND_TOPIC          // = "command"
// 盒子发布: 命令执行结果(启动/停止/复位/中止/停止营业 都用这个响应)
#define YIGONGLI_COMMANDREPLY_TOPIC            FLEXEM_COMMAND_REPLY_TOPIC    // = "commandreply"
// 盒子发布: 点位操作响应(归位/安全归位 m202/m203)
#define YIGONGLI_POINT_POST_TOPIC              FLEXEM_COMMAND_REPLY_TOPIC    // = "commandreply" 复用别名
// 盒子发布: 状态/进度/变化上报
#define YIGONGLI_MONITOR_TOPIC                 FLEXEM_MONITOR_TOPIC          // = "monitor"
// 盒子发布: 故障/报警上报(d1000 故障码 + m147/m148/m169 状态位)
#define YIGONGLI_PUSHALARM_TOPIC               FLEXEM_PUSHALARM_TOPIC        // = "pushalarm"

// 驿公里固定 PLC ID(协议层约定,云端命令 payload 中 plcId 字段填这个)
// PLC ID 不在头文件硬编码,改由 ConfigManager 注入(对称海天 cfg.plcSerial())

#endif // DEVICE_TOPICS_H
