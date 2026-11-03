#include "common/common.h"

#include "common/config/config_manager.h"
#include "common/log/log_system.h"
#include "common/path/program_path.h"
#include "common/sysmon/system_monitor.h"
#include "common/thread/thread_manager.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <thread>

namespace {

// std::signal 的 handler 里只能碰 lock-free 的原子量。bool 的 atomic 在各平台都是 lock-free 的。
std::atomic<bool> g_running{true};

void onSignal(int sig) {
    (void)sig;
    g_running.store(false);
}

}  // namespace

Common& Common::instance() {
    static Common app;
    return app;
}

std::string Common::configPath() {
    return ProgramPath::join("config.json");
}

bool Common::loadConfig() {
    const std::string cfgPath = configPath();
    if (cfgPath.empty()) {
        std::cerr << ">>> 错误: 无法确定程序目录，读不到 config.json" << std::endl;
        return false;
    }
    const bool existed = std::filesystem::exists(cfgPath);

    std::string err;
    if (!ConfigManager::instance().load(cfgPath, err)) {
        std::cerr << ">>> 错误: 配置加载失败(" << cfgPath << "): " << err << std::endl;
        return false;
    }

    if (!existed) {
        // 模板刚生成，里面的 brokerIp / boxNo 都是占位值，必须提醒现场先改。
        std::cout << ">>> 已生成配置模板 " << cfgPath
                  << "，请修改 boxNo / plcSerial.port / mqtt.* 后重启" << std::endl;
    }
    return true;
}

void Common::applyThreadBudget() {
    // 必须排在"起任何线程"之前（日志线程是第一个起的）。
    // 全项目线程都从 ThreadManager 取，超了直接拒绝启动。
    int budget = ConfigManager::instance().concurrency().maxThreads;
    if (budget <= 0) {
        budget = 16;
    }
    ThreadManager::instance().setLimit(budget);
    std::cout << ">>> 线程预算: " << budget << " 条" << std::endl;
}

void Common::installSignalHandlers() {
    // 用 std::signal 而不是 sigaction：这里只置一个 atomic<bool>，
    // 不需要 SA_RESTART 之类的精细控制，语义越简单越好。
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
}

bool Common::init(const char* argv0) {
    if (inited_) {
        return true;
    }

    // 1) 程序目录：不算服务，只是把"我住在哪儿"算出来，供后面拼绝对路径。
    //    不改进程工作目录（见头文件说明）。
    if (!ProgramPath::init(argv0)) {
        std::cerr << ">>> 错误: 无法定位程序所在目录" << std::endl;
        return false;
    }
    std::cout << ">>> 程序目录: " << ProgramPath::dir() << std::endl;

    // 2) 配置：日志装配的参数（级别 / 保留天数 / 队列容量）都来自它。
    //    这一步只解析文件，不写日志、不建目录，所以放在日志前面是安全的。
    if (!loadConfig()) {
        return false;
    }

    // 3) 日志装配：建日志目录 + sink + logger（不开线程）。
    //    返回之后 LOG_* 全项目可用 —— 消息先进队列，start() 后统一落盘。
    if (!LogSystem::instance().init()) {
        return false;
    }
    std::cout << ">>> 日志系统就绪: " << LogSystem::instance().dir() << "（此后可直接使用 LOG_*）"
              << std::endl;

    // 4) 线程预算：必须早于任何线程启动。
    applyThreadBudget();

    // 5) 系统状态监控装配：独立 logger + 独立文件 sink（同样不开线程）。
    //    失败只告警：监控本身不是业务链路的一部分，缺了它洗车机照跑。
    if (!SystemMonitor::instance().init()) {
        std::cerr << ">>> 警告: 系统状态监控装配失败，本次运行不记录 CPU/内存指标" << std::endl;
    }

    // 6) 退出信号
    installSignalHandlers();

    inited_ = true;
    return true;
}

bool Common::start() {
    if (!inited_) {
        std::cerr << ">>> 错误: 公共设施未初始化，不能启动" << std::endl;
        return false;
    }
    if (started_) {
        return true;
    }
    if (!LogSystem::instance().start()) {
        return false;
    }

    // 日志线程起来之后再启采样，保证第一条指标能立刻落盘（而不是攒在队列里）。
    // 同样只告警不拦截：采样线程起不来不该让整机起不来。
    if (!SystemMonitor::instance().start()) {
        std::cerr << ">>> 警告: 系统状态监控未启动（配置关闭或装配失败）" << std::endl;
    }

    started_ = true;
    return true;
}

void Common::stop() {
    if (!started_) {
        return;
    }
    // 顺序：先停采样，再停日志。
    // 反过来的话，采样线程会在日志线程已经 join 之后继续 push，那些行直接丢掉。
    SystemMonitor::instance().stop();
    LogSystem::instance().stop();   // 内部 join 线程并写完队列剩余消息

    // 走到这里所有线程都该已 join。留一条汇总，方便确认"没有线程泄漏"。
    std::cout << ">>> 线程已全部回收, 剩余 " << ThreadManager::instance().count()
              << " 条, 累计拒绝 " << ThreadManager::instance().rejectedCount() << " 次" << std::endl;
    started_ = false;
}

bool Common::running() const {
    return g_running.load();
}

void Common::requestStop() {
    g_running.store(false);
}

void Common::waitForExit() {
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}
