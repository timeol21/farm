#pragma once

// 全局时间工具。
//
// 以前 localtime_r / localtime_s 的 #ifdef 散在 6 个文件里（日志、上报调度、
// 两个机型、报文时间戳），改一处忘一处。统一收到这里，别处只调函数。
//
// 两类时钟要分清：
//   nowSeconds / nowMs / formatNow / todayDate / isAtMinute  —— 墙上时间（system_clock）
//   steadyMs                                                 —— 单调时间（steady_clock）
// 测耗时必须用 steadyMs，否则系统校时会算出负数。

#include <cstdint>
#include <string>

namespace washcar {
namespace timeutil {

/// Unix 秒。协议里 commandreply 的 time 字段就是它（整型，不是字符串）
std::int64_t nowSeconds();

/// Unix 毫秒
std::int64_t nowMs();

/// 格式化当前本地时间。fmt 同 strftime，默认 "%Y-%m-%d %H:%M:%S"
std::string formatNow(const char* fmt = "%Y-%m-%d %H:%M:%S");

/// 当天日期 "YYYY-MM-DD"（上报调度用来做"一天只触发一次"的判据）
std::string todayDate();

/// 当前本地时间是否落在 hour:minute 这一分钟内
bool isAtMinute(int hour, int minute);

/// 取当前本地时、分
void localHourMinute(int& hour, int& minute);

/// 单调毫秒，只用于测耗时
std::int64_t steadyMs();

/// 把 struct tm 展开成字符串（内部用，暴露给需要自定义字段的调用方）
std::string formatTime(std::int64_t unixSeconds, const char* fmt);

}  // namespace timeutil
}  // namespace washcar
