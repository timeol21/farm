#pragma once

// 跨层消息定义中心 —— 三个队列里传的东西全在这里。
//
// 下行（两段队列，两级生产者/消费者）：
//   MQTT 回调 ──RawMessage──► rawBuffer ──► 下行泵(解析) ──BusinessCommand──► bizBuffer ──► 业务分派线程
// 上行：
//   业务层 ──Report──► uplinkBuffer ──► 上行泵 ──► MQTT publish
//
// 为什么 BusinessCommand 放 common 而不是 presentation：
//   它是**解析层与业务层之间的契约**，和 PlcPointValues（data ↔ business）同一性质。
//   放 common 后，presentation 只依赖 common、business 也只依赖 common，
//   两层互不 include —— 换解析实现不用动业务，反之亦然。
//
// endpoint 字符串只在 presentation/mqtt/endpoint_map.h 出现。业务层只说 ReportKind，
// 这样换协议/换 topic 命名不用动业务代码。

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

/// 一条收到的原始报文。endpoint 已由 MqttClient 从 topic 里归一化出来
/// （"command" / "read" / "reboot"），不含前缀和分隔符。
struct RawMessage {
    std::string endpoint;
    std::string payload;
    std::int64_t recvMs{0};
};

/// 上报类别。通讯层负责映射到具体的 MQTT endpoint。
enum class ReportKind {
    MONITOR,         // 属性类点位变化 / 洗车进度
    PUSHALARM,       // 故障、报警、每日物料
    READDATA,        // read 请求的应答
    COMMAND_REPLY    // 命令执行回执
};

/// 一条待上报的报文。payload 已是最终 JSON 字符串。
struct Report {
    ReportKind kind{ReportKind::MONITOR};
    std::string payload;

    /// 是否允许被同类的更新报文替换掉。
    /// monitor / readdata 是"当前状态"，覆盖即可；故障和回执是事件，必须逐条送达。
    bool replaceable() const {
        return kind == ReportKind::MONITOR || kind == ReportKind::READDATA;
    }
};

/// 业务层往外发报文的唯一出口。
/// 业务层只知道"类别 + 内容"，不知道 topic 怎么拼、发给谁。
class IReportSink {
public:
    virtual ~IReportSink() = default;
    virtual void push(Report report) = 0;
};

// =============================================================================
// 业务消息：解析层的产物、业务层的输入
// =============================================================================

/// 业务层只认 CommandType，不再自己去 json 里找 "Manual start" 这种云端字段名。
/// 协议方言（海天用 stop、驿公里用 washcar stop）全部收在 CommandParser 里。
enum class CommandType {
    UNKNOWN = 0,
    START,          // 启动洗车
    STOP,           // 停止洗车
    RESET,          // 复位
    ABORT,          // 中止（驿公里 m206）
    STOP_WORK,      // 停止营业（驿公里 m501）
    STOP_SERVICE,   // 停止营业 2（驿公里 m510）
    RESET_SAFE,     // 安全复位（驿公里 m203）
    READ,           // 读点位，回 readdata
    REBOOT,         // 重启请求（约定只记录，不真重启）
    POINT_ON,       // 单点置位
    POINT_OFF,      // 单点复位
    POINT_STATUS,   // 单点状态
    CHANGE_MODE,    // 切换维护模式
    CLEAR_ALARM     // 清除报警
};

const char* toString(CommandType type);

struct BusinessCommand {
    CommandType type{CommandType::UNKNOWN};

    /// 原始 payload。业务层用它构造 WashCarOperation，
    /// 解析层不替业务层决定参数怎么填。
    nlohmann::json raw;

    /// READ 专用：云端要读的点位（已规范成大写软元件名）
    std::vector<std::string> points;

    /// 来源 endpoint（"command" / "read" / "reboot"），只用于日志与回执归类
    std::string source;
};
