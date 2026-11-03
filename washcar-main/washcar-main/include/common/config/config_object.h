#pragma once
#include <string>
#include <vector>
#include <map>

// 配置层类型全部放 washcar::cfg 命名空间,避免跟业务层 BoxConfig / CameraConfig 同名冲突
namespace washcar {
namespace cfg {

// ========== 摄像头(L1 拍齐版要求 ≥2:排队区 + 服务区) ==========
struct CameraConfig {
    std::string id;       // "queue" / "service",代码里靠这个区分区域
    std::string name;     // "排队区" / "服务区",展示用
    std::string url;      // 完整 RTSP URL
};

// ========== MQTT 主题(每个逻辑功能一条完整主题) ==========
// 主题里的 {boxNo} 在运行时替换成盒号。默认沿用现场现网的斜杠格式;
// 需要切成点号时只改配置,不需要重编译。
struct MqttTopicsConfig {
    std::string monitor      = "flexem/{boxNo}/monitor";
    std::string readdata     = "flexem/{boxNo}/readdata";
    std::string read         = "flexem/{boxNo}/read";
    std::string pushalarm    = "flexem/{boxNo}/pushalarm";
    std::string command      = "flexem/{boxNo}/command";
    std::string commandreply = "flexem/{boxNo}/commandreply";
    std::string reboot       = "flexem/{boxNo}/reboot";
};

// ========== MQTT(原 main_test.cpp:63-67 + mqtt_service.h:124,156,159) ==========
struct MqttConfig {
    std::string brokerIp;             // MQTT broker 地址
    short       port;                 // 1883 / 8883
    std::string username;             // 鉴权账号
    std::string password;             // 鉴权密码,支持 "${ENV_VAR}" 占位
    int         heartbeatSec;         // 心跳间隔,原 mqtt_service.h:124 默认 30s
    int         reconnectBaseSec;     // 重连基准间隔,原 :156 默认 2s
    int         reconnectMaxRetry;    // 最大重连次数,原 :159 默认 5
    MqttTopicsConfig topics;          // 7 个业务主题;老 config.json 没这一段时用默认值
};

// ========== PLC 串口参数(原 main_test.cpp:100 硬编码) ==========
struct PlcSerialConfig {
    std::string port;       // "/dev/ttyUSB10" 等
    int         baudRate;   // FX PLC 9600
    std::string parity;     // "none" / "even" / "odd"
    int         dataBits;   // FX PLC 7
    int         stopBits;   // FX PLC 1
};

// ========== PLC Modbus TCP 参数(驿公里洗车机,对称 PlcSerialConfig) ==========
struct PlcTcpConfig {
    std::string plcId;      // 协议层 plcId 标识,云端命令 payload 中 plcId 字段填这个
    std::string host;       // PLC IP,默认 192.168.31.250
    int         port;       // Modbus TCP 端口,默认 502
    int         unitId;     // Modbus 从站地址,默认 1
};

// 信号的上报通道。位掩码,允许同一个点同时进多个通道。
// 旧 config.json 里的 "property" / "event" / "none" 继续兼容。
enum class SignalReport : unsigned char {
    REPORT_NONE      = 0,
    REPORT_MONITOR   = 1u << 0,   // 变化上报 monitor
    REPORT_READDATA  = 1u << 1,   // 立即读取响应 readdata
    REPORT_PUSHALARM = 1u << 2,   // 故障/报警上报 pushalarm
    REPORT_PROPERTY  = 3,   // MONITOR | READDATA
    REPORT_EVENT     = 4    // PUSHALARM
};

inline SignalReport operator|(SignalReport a, SignalReport b) {
    return static_cast<SignalReport>(static_cast<unsigned char>(a) | static_cast<unsigned char>(b));
}
inline SignalReport operator&(SignalReport a, SignalReport b) {
    return static_cast<SignalReport>(static_cast<unsigned char>(a) & static_cast<unsigned char>(b));
}
inline bool hasReport(SignalReport mask, SignalReport channel) {
    return (static_cast<unsigned char>(mask) & static_cast<unsigned char>(channel)) != 0;
}

struct SignalConfig {
    std::string name;                // 内部语义名,如 "washing" / "fault"
    std::string point;               // PLC 软元件,如 "Y74" / "D28"
	SignalReport report = SignalReport::REPORT_NONE; // 上报通道掩码,默认不进任何通道
    std::string cloudKey;            // 上报字段名。空 = 用 point 大写,跟云端 Excel 的"属性名"列一致
    std::string comment;             // 现场看的说明
    int normalValue = 0;
};

// 一个可被云端调用的服务(命令)。write 的 key 是选择条件("default" 或 mode / safe
// 这类语义),value 是要写的软元件。
struct ServiceConfig {
    std::string name;                // 内部名,如 "start" / "reset"
    std::string cloudField;          // 云端下发的字段名,如 "Manual start" / "stop" / "reset"
    std::map<std::string, std::string> write;  // 选择条件 -> 软元件
    std::string comment;
};

// 采集清单。按软元件类型分组(采集层要走不同的读函数,X / Y 还能按组读)。
// X / Y 组读一次只回 1 字节 = 8 个位,组内位号 = 地址 % 10,只有 0~7 有效。
// 采集周期不在这里 —— 它是盒子级的运行参数,放 TimingConfig.acquisitionIntervalMs。
struct AcquisitionConfig {
    std::vector<std::string> dPoints;     // D 寄存器
    std::vector<std::string> yPoints;     // Y 输出位
    std::vector<std::string> mPoints;     // M 内部位
    std::vector<std::string> xPoints;     // X 输入位
    std::vector<std::string> sPoints;     // S 状态位
};

// 启动准入规则里"为什么没到位"的替代文案: 命中第一条就换掉 reason。
struct StartCheckReasonIf {
    std::string signal;      // 引用 signals[].name
    int expect = 1;
    std::string reason;
};

// 一条启动准入规则。规则是数据: 哪个点、期望值、读不到怎么办、回什么文案。
// kind = "signal"(默认, 看一个点位) / "activeStep"(看当前工序步号, 机型自己给)
struct StartCheckConfig {
    std::string kind = "signal";
    std::string signal;              // kind=signal 必填, 引用 signals[].name
    int expect = 1;                  // 不等于 expect 就拒绝
    std::string missing = "allow";   // 读不到点/没有步号: allow=跳过, reject=拒绝(配置必须显式写)
    std::string reason;              // 拒绝理由(activeStep 支持 {step} 占位符)
    std::string suggestion;          // 给现场的操作建议
    std::vector<StartCheckReasonIf> reasonIf;
    bool enabled = true;             // false = 停用(可用来关掉内置规则)
    // 配置里有没有显式写 missing(内置默认表由代码置位)。放在末尾: 默认表的聚合初始化按位置写。
    bool missingSpecified = false;
};

// 一台洗车机的软元件表。
struct MachinePointTable {
    std::string machineType;         // "haiTian" / "yiGongLi",必须跟 box.boxType 一致
    std::string protocol;            // "fxSerial" / "modbusTcp"
    std::string description;

    AcquisitionConfig acquisition;   // 采集哪些软元件
    std::vector<SignalConfig> signals;    // 有名字的信号,按 name 唯一
    std::vector<ServiceConfig> services;  // 云端可调用的服务
    std::vector<std::string> homeSensors;   // 复位自检的归位传感器(name)
    std::vector<std::string> dailyReportSignals;
    std::vector<std::string> faultSignals;
    std::vector<std::string> resetSignals;
    std::map<std::string, std::string> faultCodes;
    std::vector<std::string> keyStatePoints;  // 要打变化日志的点位(name)

    // 启动准入规则(按顺序判定, 第一条不满足就拒绝)。空 = 配置校验不通过。
    std::vector<StartCheckConfig> startChecks;
};

// ========== 系统 ==========
struct BoxConfig {
    std::string boxNo;       // 盒号,原 main_test.cpp:65
    std::string dbPath;      // 旧字段:SQLite 所在目录。storageRoot 为空时兜底(现场 config.json 用的就是它)
    std::string storageRoot; // 运行时持久化根目录,下面平铺 command.db / ai.db / maintenance.flag / images / log
    std::string logPath;     // 日志目录,默认 <storageRoot>/log
    std::string logLevel = "info";  // 日志级别: debug/info/warning/error
                                   // info 及以上才落盘 —— DEBUG 里是心跳(tick/MQTT 每包一条), 会刷屏
    bool plcEnabled = true;  // 开发者测试用:是否注册 PLC
    // 洗车机厂商类型,main_test 注入对应 PLC + washcar service(对称 cfg.plcSerial 走 FX 串口)
    // "haiTian"(默认,海天) / "yiGongLi"(驿公里 Modbus TCP)
    std::string boxType;
};

// ========== 本地运维账号(只用于盒子自带配置台) ==========
struct AdminConfig {
    std::string username = "admin";
    // PBKDF2-SHA256,格式: pbkdf2_sha256$<iter>$<salt-hex>$<hash-hex>
    // 空 = 还没改过密码,登录时用内置默认密码 "washcar",页面会提示修改。
    std::string passwordHash;
};

// ========== AI 检测模型配置(多模型, 每个模型独立 .rknn 路径; modelConfigs 空 = 不接 AI) ==========
// 一个 yolov8 模型文件可输出多类别, classIds 与 classTypes 按下标一一对应
struct AiRect {
    int x = 0, y = 0, w = 0, h = 0;   // 原图坐标的矩形; w<=0 = 整幅
};

struct AiModelConfig {
    std::string modelId;                    // 模型 ID, 区域通过它引用
    std::string modelPath;                  // 单文件模型的路径(只作为旧配置输入兼容, 新配置写 modelPaths)
    std::vector<std::string> modelPaths;    // 模型组成文件(有序), 空 = 配置错误(检测服务构造抛异常)
    std::vector<int> classIds;              // 本模型输出中关注的类别 id
    std::vector<std::string> classTypes;    // 对应业务类别名("CAR_PRESENT" 等, 业务层解析成枚举)
    float boxThreshold = 0.25f;             // 目标框置信度阈值(暂由 yolov8 封装内部控制)
    float nmsThreshold = 0.45f;             // NMS 阈值(暂由 yolov8 封装内部控制)
    std::string modelKind = "yolov8";       // 模型种类: yolov8(通用检测) / plate(车牌, 检测+识别两级串联)
    AiRect roi;                             // plate 的检测 ROI(原图坐标), 整幅太宽检不到车牌
};

// AI 区域-模型绑定(regionId 对齐 cameras[].id: "queue" / "service")
struct AiRegionBinding {
    std::string regionId;                   // 区域 ID = 摄像头 ID(帧缓冲按它取帧)
    std::vector<std::string> idleModelIds;      // 低频模型子集(车、人)
    std::vector<std::string> workingModelIds;   // 高频模型子集(人、车顶、车门、车窗、车牌)
};

struct AiConfig {
    bool aiEnabled = false;                     // 是否启用智能检测(false = 不加载任何模型, 同 plcEnabled 语义)
    int idleIntervalMs = 6000;                  // 低频检测周期(默认 6s)
    int workingIntervalMs = 2000;               // 高频检测周期(默认 2s)
    int exitWorkingEmptyTimes = 20;             // 高频连续 N 次无人无车 -> 回低频(20 × 2s ≈ 40s)
    int resultRetentionDays = 30;               // 检测结果保留天数
    int maxDetectionResults = 200000;          // 检测结果条数上限(只存变化, 密度低, 200000 条约 68MB)
    int maxAlarmImageMb = 500;                 // 留证原图目录总量上限(MB), 原图 JPEG 约 250KB 一张
    int alarmImageKeepDays = 7;                 // 留证原图保留天数(跟日志一致)
    int logSummaryIntervalSec = 60;              // 检测汇总日志基准间隔(秒)
    int logSummaryMaxIntervalSec = 1800;         // 退避上限(秒): 没人来洗车时最多这么久一条
    std::vector<AiModelConfig> modelConfigs;    // 模型定义(aiEnabled=true 时逐项加载; 模板里保留示例条目供填写参考)
    std::vector<AiRegionBinding> regions;       // 区域-模型对
};

struct HttpConfig {
    std::string bind;        // "0.0.0.0" / "127.0.0.1"
    short       port;        // 默认 8080
};

// ========== 时间类参数 ==========
struct TimingConfig {
    int washcarMonitorIntervalMs;       // 洗车状态轮询,原 washcar_service.cpp:888 500ms
    int washcarCommandConfirmTimeoutMs; // 驿公里命令确认点轮询超时,默认 3000(ms)
    int dailyReportHour;         // 物料日检小时,原 :951 默认 8
    int dailyReportMinute;       // 物料日检分钟,原 :951 默认 0

    // 2026-07-10 新增:3 类主动上报的轮询间隔(原硬编码 lobby_service.cpp:691,709,728)
    int dailyReportCheckIntervalMs;  // 物料日检轮询粒度:每 N ms 唤醒检查"是否到 dailyReportHour:Minute",
                                      //   到点才真正调 executeDailyReportTask()。
                                      //   默认 60000(1 分钟)。
    int faultReportIntervalMs;       // 故障轮询:每 N ms 扫故障点位,变化时上报。
                                      //   默认 3000(3 秒)。
    int valueChangeIntervalMs;       // 变化轮询:每 N ms 扫变化点位,变化时上报。
                                      //   默认 3000(3 秒)。
    // PLC 采集周期(设备服务采集一轮的间隔)。默认 500ms,现场串口 PLC 就用默认值:
    //   47 个点位 × 每秒 2 轮 ≈ 94 帧/秒,串口扛不住更密的。
    // 本地梯形图模拟器可以把周期调到 50~100ms 加快验证(见 tools/plc-mock),
    //   模拟器是本地 PTY,不受串口带宽限制。
    int acquisitionIntervalMs = 500;
    // 下限保护(由 config_manager.cpp clamp 强制,本结构不强制):
    //   faultReportIntervalMs / valueChangeIntervalMs < 100ms → 强制 100ms,避免刷屏
    //   dailyReportCheckIntervalMs < 1000ms → 强制 1000ms,1 分钟精度不需要更细
    //   acquisitionIntervalMs < 20ms → 强制 20ms(再密只是徒增串口压力)
};

// ========== 洗车状态机守卫 ==========
struct StateMachineConfig {
    // enforce(默认): 真正拒绝命令(含三态规则: 停止后必须先复位), 原因回给调用方。
    // log_only: 只记录"本应拒绝"的日志, 命令照常执行 —— 现场需要先观察一轮时用。
    std::string mode = "enforce";
    // 状态变化时打结构化日志(旧状态 -> 新状态 + 触发异常),便于离线回放
    bool logStateTransition = true;
};

// ========== 根(组合所有配置) ==========
struct DeviceConfigRoot {
    MqttConfig       mqtt;
    BoxConfig        box;
    PlcSerialConfig  plcSerial;
    HttpConfig       http;
    TimingConfig     timing;
    PlcTcpConfig     plcTcp;       // 驿公里 Modbus TCP 参数,对称 plcSerial
    AiConfig         ai;          // AI 模型配置(目前只支持 yolov8)
    StateMachineConfig stateMachine; // 洗车状态机守卫
    AdminConfig      admin;       // 本地配置台账号
    // 洗车机软元件表。按 boxType 分段:{"haiTian": {...}, "yiGongLi": {...}}。
    // 当前生效的那一份由 ConfigManager::machine() 按 box.boxType 选出;
    // 某机型没配就回落到 default_point.h 里的内置默认表。
    std::map<std::string, MachinePointTable> machines;
    std::vector<CameraConfig> cameras;
};

}  // namespace cfg
}  // namespace washcar
