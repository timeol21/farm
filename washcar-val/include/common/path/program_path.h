#pragma once

#include <string>

// =============================================================================
// ProgramPath —— 全项目唯一知道"程序自己在哪里"的地方
//
// 只做三件事：
//   init(argv0)  启动时算一次程序所在目录（绝对路径），之后不再变
//   dir()        取这个目录
//   join(相对)   把相对路径拼成绝对路径
//
// 为什么不用 chdir（历史做法，已移除）：
//   chdir 是**进程级全局副作用**。它把整个进程的当前目录改掉，所有相对路径
//   都跟着变，包括第三方库内部的 open()。副作用一旦发生，就再也回不去，
//   也没法在日志里说清"这个 ./data 到底指哪儿"。
//   本程序真正需要的其实只有两件事：config.json 在哪、data/ 在哪。
//   把这两件事变成"显式拼绝对路径"，比改全局状态更容易看懂，也不会影响别的模块。
//
// 注意：init 必须在第一次 join 之前调用。init 之前 dir() 返回空串。
// =============================================================================

class ProgramPath {
public:
    /// 计算程序所在目录。Linux 读 /proc/self/exe（软链接启动、任意目录启动都准）；
    /// 读不到时退回 argv[0] 的绝对路径所在目录。成功返回 true。
    static bool init(const char* argv0);

    /// 程序所在目录的绝对路径，不带结尾斜杠。init 之前为空串。
    static const std::string& dir();

    /// 相对路径 -> 绝对路径（相对程序目录）。空串原样返回，已是绝对路径则规范化后返回。
    static std::string join(const std::string& path);

private:
    ProgramPath() = delete;

    static std::string s_dir;
};
