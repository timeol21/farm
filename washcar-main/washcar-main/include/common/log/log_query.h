#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

// 日志读取/归类。日志文件由 FileSink 写成 log_YYYY-MM-DD.txt,
// 这里只读尾部,避免把几十 MB 的现场日志整个读进内存。
namespace log_query {

struct Entry {
    std::string time;
    std::string level;
    std::string service;
    std::string message;
};

struct Result {
    std::string date;
    std::vector<std::string> dates;
    std::vector<Entry> entries;
    std::map<std::string, std::size_t> counts;
    std::size_t total = 0;
    bool truncated = false;
};

std::string serviceLabel(const std::string& service);
std::string classifyService(const std::string& line);

// date 为空/不存在时取最新一天; service=all 表示不过滤。
Result query(const std::string& logDir,
             const std::string& date,
             const std::string& service,
             const std::string& level,
             const std::string& keyword,
             std::size_t limit);

}  // namespace log_query
