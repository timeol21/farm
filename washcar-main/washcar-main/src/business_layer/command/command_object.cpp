#include "business_layer/command/command_object.h"

#include "business_layer/lobby/lobby_service.h"
#include "common/config/config_manager.h"
#include "common/log/log_manager.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <thread>

using namespace washcar::cfg;

namespace {

std::string toFlexemPointName(const std::string& key) {
    std::string name = key;
    for (char& ch : name) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return name;
}

// LobbyResult -> Command::Result。成功时带上业务细节(故障码/点位), 失败时带错码与文案。
Command::Result toResult(const LobbyResult<WashCarOperationResult>& result) {
    Command::Result out;
    out.ok = result.success;
    if (result.success && result.data.has_value()) {
        out.code = static_cast<int>(result.data->getCode());
        out.message = result.data->getMessage();
        out.boxNo = result.data->getBoxNo();
        out.points = result.data->getStatus();
    } else {
        out.code = static_cast<int>(result.errorCode);
        out.message = result.message;
    }
    return out;
}

// read 的点位清单: 云端指定了就按指定的读, 没指定就回点位表里所有属性类点位(与改造前一致)
json buildReadPoints(const json& body) {
    json points = json::array();
    for (auto it = body.begin(); it != body.end(); ++it) {
        const std::string& key = it.key();
        if (key == "boxNo" || key == "time" || key == "plcId" || key == "reqSource" || key == "readdata") {
            continue;
        }
        points.push_back(key);
    }
    if (!points.empty()) return points;

    for (const auto& signal : ConfigManager::instance().machine().signals) {
        if (hasReport(signal.report, SignalReport::REPORT_READDATA)) {
            points.push_back(signal.point);
        }
    }
    return points;
}

}  // namespace

const char* Command::keyOf(Command::Endpoint endpoint) {
    switch (endpoint) {
    case Command::Endpoint::COMMAND:      return "command";
    case Command::Endpoint::READ:         return "read";
    case Command::Endpoint::REBOOT:       return "reboot";
    case Command::Endpoint::COMMANDREPLY: return "commandreply";
    case Command::Endpoint::READDATA:     return "readdata";
    case Command::Endpoint::MONITOR:      return "monitor";
    case Command::Endpoint::PUSHALARM:    return "pushalarm";
    }
    return "";
}

bool Command::fromKey(const std::string& key, Command::Endpoint& out) {
    static const Command::Endpoint kAll[] = {
        Command::Endpoint::COMMAND, Command::Endpoint::READ, Command::Endpoint::REBOOT,
        Command::Endpoint::COMMANDREPLY, Command::Endpoint::READDATA,
        Command::Endpoint::MONITOR, Command::Endpoint::PUSHALARM
    };
    for (Command::Endpoint endpoint : kAll) {
        if (key == Command::keyOf(endpoint)) {
            out = endpoint;
            return true;
        }
    }
    return false;
}

std::string Command::createId() {
    static std::atomic<int> sequence{1};
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    std::stringstream ss;
    ss << "CMD_" << ms << "_" << sequence++;
    return ss.str();
}

// ==================== Command 基类 ====================

Command::Command(Command::Type type, std::string deviceId)
    : m_id(Command::createId()), m_type(type), m_deviceId(std::move(deviceId)) {}

std::int64_t Command::nowSeconds() {
    return static_cast<std::int64_t>(std::time(nullptr));
}

Command::Message Command::buildReply(const Command::Result& result) const {
    json message;
    if (result.ok) {
        message["message"] = "success";
    } else {
        message["message"] = result.message.empty() ? "failed" : result.message;
        message["code"] = result.code;
    }
    message["time"] = nowSeconds();
    return Command::Message{Command::keyOf(replyEndpoint()), message.dump(), replyMessageType()};
}

// ==================== 洗车命令 ====================

WashCarCloudCommand::WashCarCloudCommand(Command::Type type, const char* name, Invoker invoker)
    : Command(type, ""), m_name(name), m_invoker(invoker) {}

bool WashCarCloudCommand::parse(const json& body) {
    m_operation = WashCarOperation(body);
    m_operation.setReqSource("mqtt");
    // 云端 Flexem 的 topic 已经带盒号, productId 只是历史字段, 保持改造前的取值
    m_operation.setProductId("flexem");
    m_operation.setDeviceNum(m_operation.getBoxNo());
    if (!m_operation.isValid()) {
        LOG_WARNING(std::string("[Command] ") + m_name + " 报文缺少必要字段, boxNo=" + m_operation.getBoxNo());
        return false;
    }
    return true;
}

Command::Result WashCarCloudCommand::execute(ILobbyService& lobby) {
    LOG_INFO(std::string("[Command] ") + m_name + " execute, boxNo=" + m_operation.getBoxNo()
             + ", cmdId=" + id());
    return toResult((lobby.*m_invoker)(m_operation));
}

Command::Message WashCarCloudCommand::reply(const Command::Result& result) const {
    return buildReply(result);
}

// ==================== read ====================

WashCarReadCommand::WashCarReadCommand() : Command(Command::Type::WASHCAR_READ, "") {}

bool WashCarReadCommand::parse(const json& body) {
    json payload = body;
    payload["points"] = buildReadPoints(body);

    m_operation = WashCarOperation(payload);
    m_operation.setReqSource("mqtt");
    m_operation.setProductId("flexem");
    m_operation.setDeviceNum(m_operation.getBoxNo());
    if (!m_operation.isValid()) {
        LOG_WARNING("[Command] WashCarReadCommand 报文缺少 plcId, boxNo=" + m_operation.getBoxNo());
        return false;
    }
    return true;
}

Command::Result WashCarReadCommand::execute(ILobbyService& lobby) {
    LOG_INFO("[Command] WashCarReadCommand execute, boxNo=" + m_operation.getBoxNo()
             + ", pointCount=" + std::to_string(m_operation.getQueryPoints().size())
             + ", cmdId=" + id());
    return toResult(lobby.getWashCarStatus(m_operation));
}

Command::Message WashCarReadCommand::reply(const Command::Result& result) const {
    json message = json::object();
    for (const auto& [key, value] : result.points) {
        message[toFlexemPointName(key)] = value;
    }
    message["boxNo"] = result.boxNo.empty() ? ConfigManager::instance().box().boxNo : result.boxNo;
    message["time"] = nowSeconds();
    if (!result.ok) {
        message["message"] = result.message.empty() ? "read failed" : result.message;
        message["code"] = result.code;
    }
    return Command::Message{Command::keyOf(replyEndpoint()), message.dump(), replyMessageType()};
}

// ==================== reboot ====================

RebootCommand::RebootCommand() : Command(Command::Type::REBOOT, "") {}

bool RebootCommand::parse(const json& body) {
    m_boxNo = body.contains("boxNo") && body["boxNo"].is_string() ? body["boxNo"].get<std::string>() : "";
    // 报文合法性在 execute 里转成业务化回执(invalid reboot payload), 与改造前一致
    m_valid = body.contains("reboot") && body["reboot"].is_number_integer() && body["reboot"].get<int>() == 1;
    return true;
}

Command::Result RebootCommand::execute(ILobbyService& lobby) {
    (void)lobby;
    Command::Result result;
    result.boxNo = m_boxNo.empty() ? ConfigManager::instance().box().boxNo : m_boxNo;

    if (!m_valid) {
        result.ok = false;
        result.code = 400;
        result.message = "invalid reboot payload";
        LOG_WARNING("[Command] RebootCommand 报文非法, boxNo=" + result.boxNo);
        return result;
    }

    result.ok = true;
    result.code = 0;
    result.message = "success";
    LOG_WARNING("[Command] RebootCommand 接受重启请求, 2s 后执行 systemctl reboot, boxNo=" + result.boxNo);

    // 先让回执发出去再重启。现场 sudoers 收敛为 ztl NOPASSWD /usr/bin/systemctl reboot。
    const std::string boxNo = result.boxNo;
    std::thread([boxNo]() {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        LOG_WARNING("[Command] executing system reboot, boxNo=" + boxNo);
        const int rc = std::system("sudo -n /usr/bin/systemctl reboot");
        if (rc != 0) {
            LOG_ERROR("[Command] systemctl reboot failed, rc=" + std::to_string(rc) + ", boxNo=" + boxNo);
        }
    }).detach();
    return result;
}

Command::Message RebootCommand::reply(const Command::Result& result) const {
    return buildReply(result);
}

// ==================== 工厂 ====================

std::shared_ptr<Command> Command::create(Endpoint endpoint, const json& body) {
    if (endpoint == Endpoint::READ) {
        return std::make_shared<WashCarReadCommand>();
    }
    if (endpoint == Endpoint::REBOOT) {
        return std::make_shared<RebootCommand>();
    }
    if (endpoint != Endpoint::COMMAND) {
        return nullptr;   // 上行端点不接收命令
    }

    // 字段名来自点位表服务项的 cloudField —— 换机型只改配置, 这里不出现机型分支。
    for (const auto& service : ConfigManager::instance().machine().services) {
        if (service.cloudField.empty()) continue;
        auto it = body.find(service.cloudField);
        if (it == body.end() || !it->is_number_integer() || it->get<int>() != 1) continue;

        // 命令的行为差异只在这张表里: (type, 日志名, 大厅方法)
        if (service.name == "start")
            return std::make_shared<WashCarCloudCommand>(Type::WASHCAR_START, "start",
                                                         &ILobbyService::startWashCar);
        if (service.name == "stop")
            return std::make_shared<WashCarCloudCommand>(Type::WASHCAR_STOP, "stop",
                                                         &ILobbyService::stopWashCar);
        if (service.name == "reset")
            return std::make_shared<WashCarCloudCommand>(Type::WASHCAR_RESET, "reset",
                                                         &ILobbyService::resetWashCar);
        if (service.name == "abort")
            return std::make_shared<WashCarCloudCommand>(Type::OTHER, "abort",
                                                         &ILobbyService::abortWashCar);
        if (service.name == "stopWork")
            return std::make_shared<WashCarCloudCommand>(Type::OTHER, "stopWork",
                                                         &ILobbyService::stopWork);
        if (service.name == "stopService")
            return std::make_shared<WashCarCloudCommand>(Type::OTHER, "stopService",
                                                         &ILobbyService::stopService);
        if (service.name == "resetSafe")
            return std::make_shared<WashCarCloudCommand>(Type::OTHER, "resetSafe",
                                                         &ILobbyService::resetSafe);
    }
    return nullptr;
}

// ==================== 载荷公共字段 ====================

void Command::fillDefaults(json& body) {
    const auto& config = ConfigManager::instance();

    const bool boxNoMissing = !body.contains("boxNo") || body["boxNo"].is_null() ||
        (body["boxNo"].is_string() && body["boxNo"].get<std::string>().empty());
    if (boxNoMissing) {
        body["boxNo"] = config.box().boxNo;
    }

    const bool plcIdMissing = !body.contains("plcId") || body["plcId"].is_null() ||
        (body["plcId"].is_string() && body["plcId"].get<std::string>().empty());
    if (plcIdMissing) {
        body["plcId"] = config.machine().protocol == "modbusTcp" ? config.plcTcp().plcId : "fx_plc_01";
    }

    body["reqSource"] = "mqtt";
}
