#pragma once
#include <cstdint>



enum class LobbyCode : int32_t {
    // ===== 成功 =====
    Success                = 0,

    // ===== 外部错误（客户端需处理） 1xxx =====
    NeedReLogin            = 1001, // 权限 / token / session 失效
    PermissionDenied       = 1002, // 有登录态但无权限
    InvalidCredential      = 1003, // 凭证非法（签名错误等）

    InvalidRequest         = 1101, // 参数错误 / 协议不合法
    UnsupportedOperation   = 1102, // 操作不支持

    // ===== 资源/业务不可满足（非系统错误）2xxx =====
    ResourceNotFound       = 2001, // 资源不存在（视频、设备等）
    ResourceExpired        = 2002, // 资源过期
    ResourceUnavailable    = 2003, // 暂不可用（占用中、离线）

    // ===== 内部错误（客户端不需理解）5xxx =====
    InternalError          = 5000, // 未分类内部错误
    ServiceUnavailable     = 5001, // 子服务不可用
    Timeout                = 5002, // 内部调用超时
};


