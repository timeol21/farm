// timer_test.cpp — 定时器自检。
// 覆盖: 两档任务、改周期、删除、任务自删不卡死、同名覆盖、启动后注册。
// 用法: ./timer_test
// 退出码: 0 = 全过, 非 0 = 失败用例数
//
// 本文件自包含: 断言助手在文件内, 不依赖其它测试文件。
#include "business_layer/timer.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

using namespace std::chrono_literals;

namespace {

int g_pass = 0;
int g_fail = 0;

void expect(bool ok, const std::string& what) {
    if (ok) {
        ++g_pass;
        return;
    }
    ++g_fail;
    std::cout << "  [FAIL] " << what << std::endl;
}

}  // namespace

int main() {
    // 1. inline 任务按周期执行, stop 后清空
    {
        Timer timer;
        std::atomic<int> count{0};
        timer.addTask("inline", 20, TimerTaskMode::INLINE, [&]() { ++count; });
        timer.start();
        std::this_thread::sleep_for(150ms);
        expect(timer.isRunning(), "start 后 isRunning=true");
        timer.stop();
        expect(count.load() >= 3, "inline 任务按周期执行");
        expect(!timer.isRunning(), "stop 后 isRunning=false");
        expect(timer.taskCount() == 0, "stop 清空任务");
    }

    // 2. worker 阻塞不拖 inline
    {
        Timer timer;
        std::atomic<int> inlineCount{0};
        std::atomic<int> workerCount{0};
        timer.addTask("inline", 20, TimerTaskMode::INLINE, [&]() { ++inlineCount; });
        timer.addTask("worker", 20, TimerTaskMode::WORKER, [&]() {
            ++workerCount;
            std::this_thread::sleep_for(60ms);
        });
        timer.start();
        std::this_thread::sleep_for(220ms);
        timer.stop();
        expect(inlineCount.load() >= 5, "worker 阻塞时 inline 照常跑");
        expect(workerCount.load() >= 2, "worker 任务按周期执行");
    }

    // 3. setInterval 改周期
    {
        Timer timer;
        std::atomic<int> count{0};
        timer.addTask("t", 200, TimerTaskMode::INLINE, [&]() { ++count; });
        timer.start();
        std::this_thread::sleep_for(80ms);
        const int first = count.load();
        timer.setInterval("t", 10);
        std::this_thread::sleep_for(120ms);
        timer.stop();
        expect(first >= 1, "首轮立即执行");
        expect(count.load() >= first + 3, "改周期后执行变密");
    }

    // 4. removeTask 生效
    {
        Timer timer;
        std::atomic<int> count{0};
        timer.addTask("t", 10, TimerTaskMode::WORKER, [&]() { ++count; });
        timer.start();
        std::this_thread::sleep_for(80ms);
        timer.removeTask("t");
        const int snapshot = count.load();
        std::this_thread::sleep_for(80ms);
        timer.stop();
        expect(count.load() == snapshot, "removeTask 后不再执行");
        expect(timer.taskCount() == 0, "removeTask 后任务数归零");
    }

    // 5. 任务在自己的线程里删自己不卡死
    {
        Timer timer;
        std::atomic<int> count{0};
        timer.addTask("self", 10, TimerTaskMode::WORKER, [&]() {
            ++count;
            timer.removeTask("self");
        });
        timer.start();
        std::this_thread::sleep_for(120ms);
        timer.stop();   // 能返回 = 没死锁
        expect(count.load() == 1, "自删任务只执行一次");
    }

    // 6. 同名覆盖不叠加
    {
        Timer timer;
        std::atomic<int> a{0};
        std::atomic<int> b{0};
        timer.addTask("dup", 10, TimerTaskMode::INLINE, [&]() { ++a; });
        timer.addTask("dup", 10, TimerTaskMode::INLINE, [&]() { ++b; });
        expect(timer.taskCount() == 1, "同名任务只留一个");
        timer.start();
        std::this_thread::sleep_for(80ms);
        timer.stop();
        expect(a.load() == 0 && b.load() >= 1, "同名覆盖成最后一次注册");
    }

    // 7. start 之后再注册 worker
    {
        Timer timer;
        std::atomic<int> count{0};
        timer.start();
        timer.addTask("late", 10, TimerTaskMode::WORKER, [&]() { ++count; });
        std::this_thread::sleep_for(90ms);
        timer.stop();
        expect(count.load() >= 1, "start 之后注册的 worker 立即生效");
    }

    std::cout << "timer_test: pass=" << g_pass << ", fail=" << g_fail << std::endl;
    return g_fail == 0 ? 0 : 1;
}
