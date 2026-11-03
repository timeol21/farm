#pragma once
// =====================================================================
// [TEMP_DIAG] 临时诊断用 trace 上下文 —— 阻塞点定位完成后整批删除
//
// 为什么需要这个：
//   MQTT 命令路径 (mqttThread -> dispatcher.handleMqtt ->
//   lobbyService.* -> washcarServiece.* -> deviceService.* ->
//   FxPlcInstance::sendAndReceive) 全部是同步调用, 整条链路在
//   一个线程里跑, 所以一个 thread_local cmdId 可以从入口一直
//   透传到最底层串口 read(), 不用改任何函数签名。
//
// 为什么不能光靠 thread id：
//   mqttThread 是循环跑的, 处理完一条命令后才接下一条, 同一线程
//   短时间内会串多条命令, 没有 cmdId 就分不清 "[FxPlcInstance]
//   SERIAL_ENTER" 到底属于哪条命令。
//
// 删除方案：
//   现场复现并抓到阻塞根因后, 一次性 grep "trace::" 和 "TEMP_DIAG"
//   把所有引用连带本文件一起删掉就行, 不影响正常业务。
// =====================================================================

#include <chrono>
#include <string>

namespace trace {

// 从 steady_clock 起点算起的单调毫秒数。
// 用法: 算两个 trace 点之间的差值 (nowMs() - enterMs),
// 不要用 std::time() —— 那个会被 NTP / 系统时钟改动影响。
//
// 示例:
//   long long enter = trace::nowMs();
//   ... 干点啥 ...
//   LOG_INFO("...durationMs=" + std::to_string(trace::nowMs() - enter));
inline long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// 一条命令一个 cmdId, 在 MQTT dispatch 入口设一次, 整条链路只读不写。
// C++17 inline 变量: 多个翻译单元 include 本头时, 每个线程只有一份
// thread_local 实例, 不会冲突。默认构造就是空串, 无需 helper 包装。
inline thread_local std::string cmdId;

} // namespace trace