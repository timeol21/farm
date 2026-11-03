#include "common/time/time.h"

#include <chrono>
#include <cstdio>
#include <ctime>

namespace washcar {
namespace timeutil {
namespace {

/// 唯一的平台分支点。其余函数都走这里，避免 #ifdef 扩散。
void toLocal(std::time_t t, std::tm& out) {
#if defined(_WIN32)
    localtime_s(&out, &t);
#else
    localtime_r(&t, &out);
#endif
}

}  // namespace

std::int64_t nowSeconds() {
    return static_cast<std::int64_t>(std::time(nullptr));
}

std::int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::int64_t steadyMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

std::string formatTime(std::int64_t unixSeconds, const char* fmt) {
    const std::time_t t = static_cast<std::time_t>(unixSeconds);
    std::tm tmNow{};
    toLocal(t, tmNow);

    char buf[64];
    const std::size_t n = std::strftime(buf, sizeof(buf), fmt, &tmNow);
    if (n == 0) {
        return {};
    }
    return std::string(buf, n);
}

std::string formatNow(const char* fmt) {
    return formatTime(nowSeconds(), fmt);
}

std::string todayDate() {
    return formatNow("%Y-%m-%d");
}

void localHourMinute(int& hour, int& minute) {
    const std::time_t t = std::time(nullptr);
    std::tm tmNow{};
    toLocal(t, tmNow);
    hour = tmNow.tm_hour;
    minute = tmNow.tm_min;
}

bool isAtMinute(int hour, int minute) {
    int h = 0;
    int m = 0;
    localHourMinute(h, m);
    return h == hour && m == minute;
}

}  // namespace timeutil
}  // namespace washcar
