#include "common/config/config_object.h"
#include "common/config/config_serialization.h"
#include "common/config/default_point.h"
#include <nlohmann/json.hpp>
#include <string>

namespace default_config {

using namespace washcar::cfg;
using json = nlohmann::json;

// 编译期默认洗车机厂商(与 config.json 的 box.boxType 一致)。
// "haiTian"(海天, 默认) / "yiGongLi"(驿公里)
// 2026-10-12 从 yiGongLi 改为 haiTian:近期现场测试都是海天。
inline const std::string& getDefaultBoxType() {
    static const std::string kDefaultBoxType = "haiTian";
    return kDefaultBoxType;
}

// 全部 .cpp 局部 static const — 改这里不会触发任何 .h 重编
static const DeviceConfigRoot kDefault = []{
    DeviceConfigRoot r;

    // ============ Box ============
    r.box.boxNo   = "123456";
    // 持久化根目录：下面平铺 command.db / ai.db / maintenance.flag,
    // 只有图片和日志这种一堆文件的才开子目录 images/ 和 log/。
    r.box.dbPath  = "./data";      // 旧字段,保留给现场已有的 config.json 读
    r.box.storageRoot = "./data";
    // 没有 logPath: 日志目录固定为 <程序目录>/data/log (见 LogSystem)
    r.box.logLevel = "info";      // DEBUG 是心跳日志, 会刷屏; 排查时临时改成 "debug"
    r.box.logKeepDays = 7;        // 只保留最近 7 天 log_*.txt, 盒子磁盘小
    r.box.plcEnabled = true;
    // 洗车机厂商类型("haiTian" / "yiGongLi"); 洗车机软元件表跟它走, 见 default_point.h
    r.box.boxType = getDefaultBoxType();

    // ============ MQTT ============
    // password 支持 "${ENV_VAR}" 占位, 见 config_manager.cpp:resolveEnvVars
    r.mqtt.brokerIp          = "127.0.0.1";
    r.mqtt.port              = 1883;
    r.mqtt.username          = "";
    r.mqtt.password          = "";
    r.mqtt.heartbeatSec      = 30;             // 心跳间隔
    r.mqtt.reconnectBaseSec  = 2;              // 重连基准间隔
    r.mqtt.reconnectMaxRetry = 5;              // 最大重连次数
    // 繁易盒子生产抓包格式是点号(flexem.123456.command); 云端若只认斜杠改成 "/"
    r.mqtt.topicSeparator    = "/";
    // 协议约定 reboot 只识别记录、不直接重启 → 默认不执行
    r.mqtt.rebootEnabled     = false;

    // ============ PLC 串口(海天) ============
    // 默认 /tmp/vplc-pty(虚拟 PLC); 切回真 PLC 改 /dev/ttyUSB10, 或写 config.json 覆盖
    r.plcSerial.port     = "/dev/ttyUSB10";
    r.plcSerial.baudRate = 9600;
    r.plcSerial.parity   = "even";
    r.plcSerial.dataBits = 7;
    r.plcSerial.stopBits = 1;
    // 单帧读超时 200ms。原实现是 VTIME=10(1 秒), PLC 不应答时每点白等 1 秒,
    // 一轮 37 次读就把采集线程拖死 —— 这里降到 200ms, 正常往返只要 20~30ms
    r.plcSerial.timeoutMs    = 200;
    // 帧间静默 10ms; RTU 要求 >= 3.5 字符时间, 代码里兜底不小于 4ms
    r.plcSerial.frameDelayMs = 10;

    // ============ Modbus TCP(驿公里洗车机) — 对称 cfg.plcSerial ============
    // 默认连驿公里 PLC:192.168.31.250:502 unitId=1(与 TcpPlcInstance 构造函数默认一致)
    r.plcTcp.plcId  = "tcp_plc_01";
    r.plcTcp.host   = "192.168.31.250";  //192.168.1.100
    r.plcTcp.port   = 502;
    r.plcTcp.unitId = 1;
    // 单个 Modbus 请求收发超时 2 秒
    r.plcTcp.timeoutMs = 2000;

    // ============ 洗车机软元件表 ============
    // 默认不往模板里写 machine 段:内置默认表在 default_point.cpp,
    // 缺这一段时按 box.boxType 直接用内置表。现场要改点位才手工加
    // "machine": {"haiTian": {...}} 这种分段。

    // ============ 层间队列容量 ============
    // 只在"消息被丢"的排查场景需要调; 平时保持默认
    r.queueCapacity.downCapacity = 256;   // 下行原始报文
    r.queueCapacity.upCapacity   = 128;   // 上行待发报文
    r.queueCapacity.logCapacity  = 4096;  // 日志队列, 满了丢最旧, 防止磁盘卡住时内存无限涨

    // ============ 盒子自身状态监控 ============
    // 每 5 秒往 data/log/sys_YYYY-MM-DD.txt 写一行 CPU/内存/温度/磁盘。
    // 一天约 1.7 万行、3~4MB, 按 box.logKeepDays 清理。
    r.sysmon.enabled    = true;
    r.sysmon.intervalMs = 5000;

    // ============ 线程预算 ============
    // 所有线程都从 ThreadManager 取, 这里封顶。正常约 8 条, 留一倍余量。
    r.concurrency.maxThreads = 16;

    // ============ 时间类参数 ============
    r.timing.washcarMonitorIntervalMs = 500;   // 洗车状态轮询
    r.timing.washcarCommandConfirmTimeoutMs = 3000;  // 驿公里命令确认超时
    r.timing.dailyReportHour   = 8;     // 物料日检小时
    r.timing.dailyReportMinute = 0;
    // 3 类主动上报的轮询间隔(到点才真正执行上报)
    r.timing.dailyReportCheckIntervalMs = 60000;   // 物料日检轮询粒度(1 分钟检查一次是否到 8:00)
    r.timing.faultReportIntervalMs      = 3000;    // 故障轮询间隔
    r.timing.valueChangeIntervalMs      = 3000;    // 变化轮询间隔

    // ============ 洗车状态机守卫 ============
    // 默认 log_only: 只记"本应拒绝"的日志,不改现场行为。
    // 现场确认点位采集和判定都正常后再改 enforce。
    r.stateMachine.mode = "log_only";
    r.stateMachine.logStateTransition = true;

    // ============ HTTP ============
    r.http.bind = "0.0.0.0";
    r.http.port = 8080;

    // ============ 摄像头(L1 拍齐版要求 ≥2:排队区 + 服务区) ============
    // id 必须用 "queue" / "service",代码里靠这个区分区域
    // url 现场根据实际摄像头修改(海康固定格式已对齐)
    r.cameras.push_back({
        "queue",                                  // 排队区
        "实验室摄像头(萤石云)",
        "rtsp://admin:QBKGWT@192.168.31.108:554/h264/ch1/main/av_stream"
    });
    r.cameras.push_back({
        "service",                                // 服务区
        "实验室摄像头(海康)",
        "rtsp://krdn:ph4ctc@192.168.31.208:554/Streaming/Channels/101"
    });

    // ============ AI 检测 ============
    // modelConfigs 空 = 不接 AI 检测; 模板里保留示例条目供现场填写参考
    // (示例只是占位, aiEnabled=false 默认不加载; 现场在 config.json 覆盖为真实模型)
    r.ai.aiEnabled = true;
    r.ai.idleIntervalMs = 6000;      // 低频检测周期
    r.ai.workingIntervalMs = 2000;   // 高频检测周期
    r.ai.exitWorkingEmptyTimes = 20; // 高频连续 N 次无人无车 -> 回低频(20 × 2s ≈ 40s)
    // 保存上限: 现场磁盘有限, 检测结果与异常图片都要有封顶(跟日志一个思路)
    r.ai.resultRetentionDays = 30;      // 检测结果保留天数
    r.ai.maxDetectionResults = 200000;  // 条数上限(只存变化, 200000 条约 68MB)
    r.ai.maxAlarmImageMb = 500;         // 留证原图目录总量上限(MB)
    r.ai.alarmImageKeepDays = 7;        // 留证原图保留天数(跟日志一致)
    r.ai.logSummaryIntervalSec = 60;    // 检测汇总日志基准间隔(秒)
    r.ai.logSummaryMaxIntervalSec = 1800;  // 退避上限(秒)

    washcar::cfg::AiModelConfig yolov8;
    yolov8.modelId = "yolov8";
    yolov8.modelKind = "yolov8";
    yolov8.modelPaths = {"lib/rknn/models/yolov8.rknn"};
    yolov8.classIds = {0, 2, 3, 1};   // person / car / motorcycle / bicycle
    yolov8.classTypes = {"PERSON_DETECTED", "CAR_PRESENT", "MOTORCYCLE", "BICYCLE"};
    r.ai.modelConfigs.push_back(yolov8);

    // 车牌是两级串联, modelPaths 顺序即 [检测, 识别];
    // ROI 圈住洗车机出口侧的车牌位置, 整幅 1920x1080 太宽, 车牌在里面太小检不到
    washcar::cfg::AiModelConfig plate;
    plate.modelId = "plate";
    plate.modelKind = "plate";
    plate.modelPaths = {"lib/rknn/models/ppocrv4_det.rknn", "lib/rknn/models/lprnet.rknn"};
    plate.classIds = {0};
    plate.classTypes = {"LICENSE_PLATE"};
    plate.roi = {690, 500, 220, 200};
    r.ai.modelConfigs.push_back(plate);

    r.ai.regions.push_back({
        "service",
        {"yolov8"},
        {"yolov8", "plate"}
    });

    return r;
}();

// 暴露给 ConfigManager::applyDefaults() 调。inline 让多次 include 时链接器合并符号,
// 避免测试 cpp 同时 #include 本文件和 config_manager.cpp 时的 ODR 冲突。
inline const DeviceConfigRoot& get() { return kDefault; }

// 单一事实来源: kDefault 序列化即模板(JSON 化逻辑在 config_serialization.h,
// 加字段只改结构 + 该结构 to_json/from_json, 模板自动跟随, 不再双维护)。
inline std::string toJson() {
    return json(get()).dump(2);   // 缩进 2 空格,人工编辑友好
}

}  // namespace default_config
