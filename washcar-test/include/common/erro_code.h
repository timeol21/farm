#ifndef CODE_H
#define CODE_H

#include <string>
#include <unordered_map>

// 错误码定义命名空间，避免全局命名冲突
namespace ErrorCode {

// 错误码枚举类 - 按错误类型分区间管理
// 区间规划：
// 0: 通用成功
// 1000-1999: Web请求相关错误（HTTP请求、参数、解析等）
// 2000-2999: 安全认证相关错误（登录、权限、token等）
// 3000-3999: 服务内部错误（业务逻辑、第三方依赖、资源等）
// 4000-4999: 摄像头/帧查询业务错误（原有业务域保留）
enum class Code {
    // 通用成功
    SUCCESS = 0,

    // Web请求相关错误 (1000-1999)
    WEB_PARAM_ERROR = 1001,          // 请求参数错误（格式/类型/必填项缺失）
    WEB_JSON_PARSE_ERROR = 1002,     // JSON解析失败（格式错误/数据不完整）
    WEB_INVALID_REQUEST = 1003,      // 请求无效（HTTP方法错误/路径不存在）
    WEB_REQUEST_TIMEOUT = 1004,      // 请求超时（客户端请求响应超时）
    WEB_TOO_MANY_REQUESTS = 1005,    // 请求过于频繁（限流/熔断触发）

    // 安全认证相关错误 (2000-2999)
    AUTH_TOKEN_INVALID = 2001,       // Token无效（过期/伪造/格式错误）
    AUTH_TOKEN_MISSING = 2002,       // Token缺失（请求头未携带认证信息）
    AUTH_PERMISSION_DENIED = 2003,   // 权限不足（无接口/资源访问权限）
    AUTH_USER_NOT_EXIST = 2004,      // 用户不存在（认证主体不存在）
    AUTH_USER_LOCKED = 2005,         // 用户被锁定（账号禁用/冻结）

    // 服务内部错误 (3000-3999)
    SERVER_INTERNAL_ERROR = 3001,    // 服务器内部错误（未知异常）
    SERVER_DB_ERROR = 3002,          // 数据库操作失败（连接/查询/写入异常）
    SERVER_THIRD_PARTY_ERROR = 3003, // 第三方服务调用失败（接口/网络异常）
    SERVER_RESOURCE_EXHAUSTED = 3004,// 服务器资源耗尽（内存/CPU/连接数超限）
    SERVER_CONFIG_ERROR = 3005,      // 配置错误（参数配置缺失/错误）
    SERVER_TOO_MANY_Repeat = 3006,   //上传定时器打开重复

    // 摄像头/帧查询相关错误 (4000-4999)
    CAMERA_FRAME_QUERY_INVALID = 4001,  // 帧查询参数无效（摄像头ID/时间范围非法）
    CAMERA_NOT_FOUND = 4002,            // 摄像头不存在（未注册/已删除）
    CAMERA_OFFLINE = 4003,              // 摄像头离线（网络断开/设备故障）
    CAMERA_FRAME_RETRIEVE_FAILED = 4004,// 帧数据获取失败（存储异常/读取失败）
    CAMERA_NO_REAL_IMAGE_DATA = 4005,   // 无实时图像数据（未采集到画面/采集异常）

    // 洗车业务错误 (5000-5999)
    WASHCAR_MAINTAINING              = 5001, // 维护模式拒绝云端控制
    WASHCAR_NO_MODE                  = 5002, // 启动模式字段缺失或非法
    WASHCAR_NO_PLC                   = 5003, // PLC 不可用
    WASHCAR_START_PREPARE_FAILED     = 5004, // 启动准备(M204)失败
    WASHCAR_PLC_COMMAND_FAILED       = 5005, // PLC 命令被拒
    WASHCAR_JOG_EXECUTING            = 5006, // 点动命令冲突
    WASHCAR_JOG_POINT_REQUIRED       = 5007, // 点动模式缺 point
    WASHCAR_INVALID_POINT_FORMAT     = 5008, // point 格式非法
    WASHCAR_INVALID_POINT_ADDRESS    = 5009, // point 地址非法
    WASHCAR_INVALID_MODE             = 5010, // 模式非法
    WASHCAR_ESTOP_NOT_PRESSED        = 5011, // 急停未按
    WASHCAR_POINT_RESET_UNSUPPORTED  = 5012, // pointReset 不支持的 point
    WASHCAR_STOP_FAILED              = 5013, // stopWashCar 失败
    WASHCAR_RESET_FAILED             = 5014, // resetWashCar 失败
    WASHCAR_ABORT_FAILED             = 5015, // abortWashCar 失败
    WASHCAR_STOP_WORK_FAILED         = 5016, // stopWork 失败
    WASHCAR_STOP_SERVICE_FAILED      = 5017, // stopService 失败
    WASHCAR_RESET_SAFE_FAILED        = 5018, // resetSafe 失败
    WASHCAR_POINT_ON_FAILED          = 5019, // pointOn 失败
    WASHCAR_POINT_OFF_FAILED         = 5020, // pointOff 失败
    WASHCAR_STOPPED                  = 5021, // stopWashCar 成功（业务层特殊成功状态,区别于 SUCCESS=0;LobbyService 不算失败）

    // 驿公里命令确认层错误码
    WASHCAR_START_REJECTED           = 5022, // 启动被拒:非待机中 / 暂停服务(M510=1) / 车未停好(M282=0)
    WASHCAR_STOP_INVALID_WHILE_WASHING = 5023, // 洗车中(S20~S81)停止无效
    WASHCAR_RESET_REJECTED           = 5024, // 复位(归位)被拒:非停止态 / 硬件故障(M148=1) / 停止营业(M501=1)
    WASHCAR_RESET_SAFE_REJECTED      = 5025, // 安全归位被拒:停车位有车(M147=1)
    WASHCAR_COMMAND_TIMEOUT          = 5026, // 命令确认点超时未达成
    WASHCAR_ABORT_INVALID_NOT_WASHING = 5027, // 非洗车中(S0~S5 基础态)中止无效
    WASHCAR_STATE_MACHINE_REJECTED    = 5028  // 状态机前置检查拒绝(enforce 模式)
};

// 错误码与描述信息的映射表
// 优化点：constexpr + inline 提升编译期性能，描述更精准便于前端/日志展示
static inline const std::unordered_map<Code, std::string> CodeMessageMap = {
    {Code::SUCCESS, "操作成功"},

    // Web请求错误
    {Code::WEB_PARAM_ERROR, "请求参数错误（格式/类型不正确或必填项缺失）"},
    {Code::WEB_JSON_PARSE_ERROR, "JSON格式解析失败（数据格式错误或不完整）"},
    {Code::WEB_INVALID_REQUEST, "请求无效（HTTP方法错误或请求路径不存在）"},
    {Code::WEB_REQUEST_TIMEOUT, "请求超时（服务器未在规定时间内响应）"},
    {Code::WEB_TOO_MANY_REQUESTS, "请求过于频繁，请稍后再试（限流触发）"},

    // 安全认证错误
    {Code::AUTH_TOKEN_INVALID, "认证Token无效（已过期/伪造或格式错误）"},
    {Code::AUTH_TOKEN_MISSING, "请求头缺失认证Token，请先登录"},
    {Code::AUTH_PERMISSION_DENIED, "权限不足，无访问该资源的权限"},
    {Code::AUTH_USER_NOT_EXIST, "认证用户不存在，请检查账号信息"},
    {Code::AUTH_USER_LOCKED, "用户账号已被锁定，请联系管理员解锁"},

    // 服务内部错误
    {Code::SERVER_INTERNAL_ERROR, "服务器内部错误，请稍后重试"},
    {Code::SERVER_DB_ERROR, "数据库操作失败（连接/查询/写入异常）"},
    {Code::SERVER_THIRD_PARTY_ERROR, "第三方服务调用失败，请检查依赖服务状态"},
    {Code::SERVER_RESOURCE_EXHAUSTED, "服务器资源耗尽，暂时无法处理请求"},
    {Code::SERVER_CONFIG_ERROR, "服务器配置错误，请联系管理员检查配置"},

    // 摄像头业务错误
    {Code::CAMERA_FRAME_QUERY_INVALID, "帧查询参数无效（缺少摄像头ID或时间范围非法）"},
    {Code::CAMERA_NOT_FOUND, "指定的摄像头不存在（未注册或已删除）"},
    {Code::CAMERA_OFFLINE, "指定的摄像头已离线（网络异常或设备故障）"},
    {Code::CAMERA_FRAME_RETRIEVE_FAILED, "摄像头帧数据获取失败（存储或读取异常）"},
    {Code::CAMERA_NO_REAL_IMAGE_DATA, "暂无实时图像数据（摄像头未采集到画面）"},

    // 洗车业务错误
    {Code::WASHCAR_MAINTAINING,             "维护模式拒绝云端控制"},
    {Code::WASHCAR_NO_MODE,                 "启动模式字段缺失或非法"},
    {Code::WASHCAR_NO_PLC,                  "PLC 不可用"},
    {Code::WASHCAR_START_PREPARE_FAILED,    "启动准备失败(M204)"},
    {Code::WASHCAR_PLC_COMMAND_FAILED,      "PLC 命令被拒"},
    {Code::WASHCAR_JOG_EXECUTING,           "已有点动命令正在执行"},
    {Code::WASHCAR_JOG_POINT_REQUIRED,      "点动模式需要点位参数"},
    {Code::WASHCAR_INVALID_POINT_FORMAT,    "点位格式非法"},
    {Code::WASHCAR_INVALID_POINT_ADDRESS,   "点位地址非法"},
    {Code::WASHCAR_INVALID_MODE,            "模式参数非法"},
    {Code::WASHCAR_ESTOP_NOT_PRESSED,       "急停未按下"},
    {Code::WASHCAR_POINT_RESET_UNSUPPORTED, "点位重置暂不支持"},
    {Code::WASHCAR_STOP_FAILED,             "停止命令失败"},
    {Code::WASHCAR_RESET_FAILED,            "复位命令失败"},
    {Code::WASHCAR_ABORT_FAILED,            "中止命令失败"},
    {Code::WASHCAR_STOP_WORK_FAILED,        "停止工作失败"},
    {Code::WASHCAR_STOP_SERVICE_FAILED,     "停止服务失败"},
    {Code::WASHCAR_RESET_SAFE_FAILED,       "安全复位失败"},
    {Code::WASHCAR_POINT_ON_FAILED,         "点位开启失败"},
    {Code::WASHCAR_POINT_OFF_FAILED,        "点位关闭失败"},
    {Code::WASHCAR_STOPPED,                 "已停止"},
    // 驿公里命令确认层错误
    {Code::WASHCAR_START_REJECTED,          "启动被拒:非待机/暂停服务中/车未停好"},
    {Code::WASHCAR_STOP_INVALID_WHILE_WASHING, "洗车中停止无效,需急停或使用中止洗车"},
    {Code::WASHCAR_RESET_REJECTED,          "复位被拒:非停止态/硬件故障/停止营业中"},
    {Code::WASHCAR_RESET_SAFE_REJECTED,     "安全归位被拒:停车位有车"},
    {Code::WASHCAR_COMMAND_TIMEOUT,         "命令确认超时"},
    {Code::WASHCAR_ABORT_INVALID_NOT_WASHING, "当前不在洗车中,无需中止"}
};

/**
 * @brief 根据错误码获取对应的描述信息
 * @param code 错误码枚举值
 * @return 错误描述字符串，若未找到则返回"未知错误"
 */
inline std::string getMessage(Code code) {
    auto it = CodeMessageMap.find(code);
    return (it != CodeMessageMap.end()) ? it->second : "未知错误";
}

/**
 * @brief 将错误码转换为整数（便于网络传输/JSON序列化）
 * @param code 错误码枚举值
 * @return 错误码对应的整数值
 */
inline int toInt(Code code) {
    return static_cast<int>(code);
}

} // namespace ErrorCode

#endif // CODE_H
