#include "common/path/program_path.h"

#include <filesystem>

std::string ProgramPath::s_dir;

bool ProgramPath::init(const char* argv0) {
    std::error_code ec;

    // Linux 上 /proc/self/exe 是指向真实可执行文件的软链接。
    // 软链接启动、从任意目录用绝对路径启动，两种情形都能拿到正确结果，
    // 比 argv[0] 可靠（argv[0] 可能只是 "main" 或相对路径）。
    const auto link = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec && !link.empty()) {
        s_dir = link.parent_path().lexically_normal().string();
        return !s_dir.empty();
    }

    // 兜底：把 argv[0] 转成绝对路径，再取父目录。
    const auto abs = std::filesystem::absolute(argv0 ? argv0 : "main", ec);
    if (ec) {
        return false;
    }
    s_dir = abs.parent_path().lexically_normal().string();
    return !s_dir.empty();
}

const std::string& ProgramPath::dir() {
    return s_dir;
}

std::string ProgramPath::join(const std::string& path) {
    if (path.empty()) {
        return std::string();
    }
    std::filesystem::path p(path);
    if (p.is_absolute()) {
        return p.lexically_normal().string();
    }
    // 相对路径一律以"程序所在目录"为基准，与进程当前工作目录无关。
    return (std::filesystem::path(s_dir) / p).lexically_normal().string();
}
