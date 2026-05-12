
#ifndef MQTT_TOPICS_H  // 头文件保护：防止重复包含导致编译错误
#define MQTT_TOPICS_H

// 替换const std::string为宏定义（字符串宏需用双引号包裹）
#define GET_REAL_IMAGE_TOPIC                    "/device/camera/getRealImage"
#define OPERATE_PLC_TOPIC                       "/device/plc/operate"
#define UPDATE_CONFIG_TOPIC                     "/device/config/update"
// #define GET_SENSOR_DATA_TOPIC                   "/device/sensor/status"
#define GET_ALL_DEVICE_STATUS_TOPIC             "/device/status/getall"
// #define OPERATE_CAR_TOPIC                       "/device/carControl"
#define OPERATE_PLC_WITH_VERIFY_TOPIC           "/device/plc/operateWithVerify"
// #define UPDATE_CONFIG                           "/device/config"
#define GET_VIDEO_HISTORY_TOPIC                 "/device/camera/getVideoHistory"
#define GET_VIDEO_HISTORY_FILE_TOPIC            "/device/camera/getVideoHistoryFile"
// #define DOOR_LOCK_CONTROL_TOPIC                 "/device/doorLock/control"
// #define RESULT_DOOR_LOCK_CONTROL_TOPIC          "/device/doorLook/control/result"

#define RESULT_GET_REAL_IMAGE_TOPIC             "/device/camera/getRealImage/result"
#define RESULT_OPERATE_PLC_TOPIC                "/device/plc/operate/result"
// #define RESULT_UPDATE_CONFIG_TOPIC              "/device/config/update/result"
// #define RESULT_GET_SENSOR_DATA_TOPIC            "/device/sensor/status/result"
#define RESULT_GET_ALL_DEVICE_STATUS_TOPIC      "/device/status/getall/result"
#define RESULT_OPERATE_CAR_TOPIC                "/device/carControl/result"
#define RESULT_OPERATE_PLC_WITH_VERIFY_TOPIC    "/device/plc/operateWithVerify/result"
// #define RESULT_UPDATE_CONFIG                    "/device/config/result"
#define RESULT_VIDEO_HISTORY                    "/device/camera/getVideoHistory/result"
#define RESULT_VIDEO_HISTORY_FILE_TOPIC         "/device/camera/getVideoHistoryFile/result"
#define RESULT_AI_ALARM_TOPIC                   "/device/camera/aiAlarm/result"
#define SENSOR_ALARM_TOPIC                      "/device/sensor/alarm"

#define WASHCAR_COMMAND_REPLY_TOPIC             "carwash/%s/commandreply"
#define WASHCAR_COMMAND_TOPIC                   "carwash/%s/command"
#define WASHCAR_MONITOR_TOPIC                   "carwash/%s/monitor"
#define WASHCAR_PUSH_ALARM_TOPIC                "carwash/%s/pushalarm"
#define WASHCAR_READ_DATA_TOPIC                 "carwash/%s/readdata"







#endif // DEVICE_TOPICS_H