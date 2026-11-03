#include "common/log/log_query.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

namespace log_query {
namespace {

constexpr std::size_t kTailBytes = 2 * 1024 * 1024;

struct ParsedLine {
    std::string time;
    std::string level;
    std::string message;
};

ParsedLine parseLine(const std::string& line) {
    static const std::regex pattern(
        R"(^\[([^\]]+)\] \[([A-Z]+)\] \[T:[^\]]+\](?: \[([^\]]*)\])? (.*)$)");
    ParsedLine parsed;
    std::smatch match;
    if (std::regex_match(line, match, pattern)) {
        parsed.time = match[1].str();
        parsed.level = match[2].str();
        parsed.message = match[4].str();
    } else {
        parsed.message = line;
        parsed.level = "INFO";
    }
    return parsed;
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool readTail(const std::string& path, std::string& out, bool& truncated) {
    truncated = false;
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) return false;
    const std::streamoff size = in.tellg();
    if (size <= 0) { out.clear(); return true; }
    const std::streamoff start = size > static_cast<std::streamoff>(kTailBytes)
        ? size - static_cast<std::streamoff>(kTailBytes) : 0;
    truncated = start > 0;
    in.seekg(start, std::ios::beg);
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    if (start > 0) {
        const std::size_t pos = out.find('\n');
        if (pos != std::string::npos) out.erase(0, pos + 1);
    }
    return true;
}

bool validDate(const std::string& date) {
    if (date.size() != 10) return false;
    for (std::size_t i = 0; i < date.size(); ++i) {
        if (i == 4 || i == 7) {
            if (date[i] != '-') return false;
        } else if (!std::isdigit(static_cast<unsigned char>(date[i]))) {
            return false;
        }
    }
    return true;
}

std::vector<std::string> listDates(const std::string& logDir) {
    std::vector<std::string> dates;
    std::error_code ec;
    if (!std::filesystem::exists(logDir, ec)) return dates;
    for (const auto& entry : std::filesystem::directory_iterator(logDir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        const std::string name = entry.path().filename().string();
        if (name.rfind("log_", 0) != 0 || name.size() != 18 ||
            name.substr(name.size() - 4) != ".txt") {
            continue;
        }
        const std::string date = name.substr(4, 10);
        if (validDate(date)) dates.push_back(date);
    }
    std::sort(dates.begin(), dates.end(), std::greater<std::string>());
    dates.erase(std::unique(dates.begin(), dates.end()), dates.end());
    return dates;
}

}  // namespace

std::string serviceLabel(const std::string& service) {
    if (service == "lobby") return "大厅/状态机";
    if (service == "washcar") return "洗车服务";
    if (service == "mqtt") return "MQTT/命令";
    if (service == "plc") return "PLC/采集";
    if (service == "detection") return "AI/摄像头";
    if (service == "config") return "配置台";
    if (service == "auth") return "登录鉴权";
    return "系统/其他";
}

std::string classifyService(const std::string& line) {
    // 先认日志正文里的方括号 tag(现有代码都带 tag),避免正文里偶然提到
    // "MQTT"/"Config" 把服务归错类。
    const std::pair<const char*, const char*> tags[] = {
        {"[HaiTianWashCarService]", "washcar"},
        {"[YiGongLiWashCarService]", "washcar"},
        {"[WashCarServiceBase]", "washcar"},
        {"[LobbyService]", "lobby"},
        {"[WashCarStateMachine]", "lobby"},
        {"[MqttService]", "mqtt"},
        {"[MQTT]", "mqtt"},
        {"[CommandService]", "mqtt"},
        {"[MessageBuffer]", "mqtt"},
        {"[FxDeviceAcquisitionTask]", "plc"},
        {"[TcpPlcDeviceAcquisitionTask]", "plc"},
        {"[FxPlcInstance]", "plc"},
        {"[TcpPlcInstance]", "plc"},
        {"[DeviceService]", "plc"},
        {"[DeviceStatusCache]", "plc"},
        {"[DetectionService]", "detection"},
        {"[Detection]", "detection"},
        {"[Camera]", "detection"},
        {"[ConfigUI]", "config"},
        {"[ConfigManager]", "config"},
        {"[Config]", "config"},
        {"[Auth]", "auth"}
    };
    for (const auto& [tag, service] : tags) {
        if (line.find(tag) != std::string::npos) return service;
    }

    // 兜底:没有标准 tag 的老日志按关键字猜。
    if (line.find("HaiTianWashCarService") != std::string::npos ||
        line.find("YiGongLiWashCarService") != std::string::npos) return "washcar";
    if (line.find("LobbyService") != std::string::npos ||
        line.find("LOBBY_") != std::string::npos) return "lobby";
    if (line.find("MqttService") != std::string::npos ||
        line.find("MQTT") != std::string::npos) return "mqtt";
    if (line.find("AcquisitionTask") != std::string::npos) return "plc";
    if (line.find("Detection") != std::string::npos ||
        line.find("Camera") != std::string::npos) return "detection";
    if (line.find("Config") != std::string::npos) return "config";
    if (line.find("Auth") != std::string::npos) return "auth";
    return "system";
}

Result query(const std::string& logDir,
             const std::string& date,
             const std::string& service,
             const std::string& level,
             const std::string& keyword,
             std::size_t limit) {
    Result result;
    result.dates = listDates(logDir);
    if (result.dates.empty()) return result;

    result.date = date;
    if (result.date.empty() ||
        std::find(result.dates.begin(), result.dates.end(), result.date) == result.dates.end()) {
        result.date = result.dates.front();
    }

    const std::string path = (std::filesystem::path(logDir) /
                              ("log_" + result.date + ".txt")).string();
    std::string text;
    if (!readTail(path, text, result.truncated)) return result;

    const std::string lowerKeyword = toLower(keyword);
    std::istringstream stream(text);
    std::string line;
    std::vector<Entry> matched;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        const ParsedLine parsed = parseLine(line);
        const std::string svc = classifyService(line);
        ++result.counts[svc];

        if (!level.empty() && parsed.level != level) continue;
        if (!lowerKeyword.empty() && toLower(line).find(lowerKeyword) == std::string::npos) continue;
        result.total++;

        if (!service.empty() && service != "all" && svc != service) continue;
        Entry entry;
        entry.time = parsed.time;
        entry.level = parsed.level;
        entry.service = svc;
        entry.message = parsed.message;
        matched.push_back(std::move(entry));
    }

    if (limit == 0) limit = 300;
    if (limit > 1000) limit = 1000;
    if (matched.size() > limit) {
        matched.erase(matched.begin(), matched.end() - static_cast<std::ptrdiff_t>(limit));
    }
    result.entries = std::move(matched);
    return result;
}

}  // namespace log_query
