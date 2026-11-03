#pragma once

// =============================================================================
// Common —— 公共设施入口：整个程序跑起来之前必须准备好的那些东西
//
// 只管"什么时候做"，不管"怎么实现"（实现都在各自的子模块里）：
//
//   init(argv0)  —— 找程序目录 -> 读 config.json -> 装配日志 -> 线程预算
//                   -> 装配系统监控 -> 装退出信号
//   start()      —— 启动日志线程、dump 生效配置、启动指标采样
//   stop()       —— 先停采样，再停日志并 flush（放最后调，让各层收尾日志都落盘）
//   waitForExit()
//
// 为什么 init 和 start 要分开：
//   读配置、建目录、开文件都是"初始化"——一次性、会失败、失败要当场知道；
//   起后台线程是"启动"——开始工作，和三层 start() 在同一抽象层级。
//   合起来会出现"配置读完了但日志线程起不来"的半截状态。
//
// 为什么这里没有 chdir：
//   进程工作目录是全局副作用，改了之后所有相对路径的含义都跟着变，日志里也说不清。
//   现在改成：由 ProgramPath 算出程序目录，配置、日志、数据各自拼绝对路径。
//   谁需要文件，谁就显式写清楚自己要哪个绝对路径，不依赖"当前目录恰好是哪儿"。
//
// 初始化顺序里唯一不能动的一步：
//   配置必须先于日志装配。因为日志的级别、保留天数、队列容量本身就写在 config.json 里，
//   不先读配置，日志只能先按写死的值装起来。反过来讲：日志是第一个"装配完就能用"的
//   子系统 —— 它一返回，全项目任何地方都可以直接 LOG_INFO()，不必等 main 往下走。
//   配置解析本身不写日志（失败走 stderr），所以它挡在日志前面不会造成"日志丢失"。
//
// main 里的用法：
//   Common& app = Common::instance();
//   if (!app.init(argv[0])) return -1;
//   if (!app.start())       return -1;
//   ...
//   app.waitForExit();
//   ... 三层 stop ...
//   app.stop();
// =============================================================================

#include <string>

class Common {
public:
    static Common& instance();

    /// 程序目录 + 读配置 + 装配日志 + 线程预算 + 装配系统监控 + 退出信号。失败返回 false。
    bool init(const char* argv0);

    /// 启动日志工作线程（此后日志真正落盘）与指标采样线程，并 dump 生效配置。
    bool start();

    /// 停采样线程 + 停日志线程并 flush 队列剩余消息。
    void stop();

    /// 是否还在跑（收到 SIGINT/SIGTERM 后变 false）
    bool running() const;

    /// 阻塞直到收到退出信号（200ms 轮询，避免忙等）
    void waitForExit();

    /// 主动请求退出
    void requestStop();

    bool inited() const { return inited_; }
    bool started() const { return started_; }

    /// config.json 的绝对路径（ProgramPath 就绪后可取）
    static std::string configPath();

private:
    Common() = default;
    Common(const Common&) = delete;
    Common& operator=(const Common&) = delete;

    bool loadConfig();
    void applyThreadBudget();
    static void installSignalHandlers();

    bool inited_{false};
    bool started_{false};
};
