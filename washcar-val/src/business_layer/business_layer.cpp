#include "business_layer/business_layer.h"

#include "business_layer/report/report_scheduler.h"
#include "business_layer/washcar/washcar_service.h"
#include "common/config/config_manager.h"
#include "common/log/log_manager.h"
#include "common/time/time.h"
#include "data_layer/idevice_service.h"

#include <csignal>
#include <exception>
#include <iostream>
#include <string>

BusinessLayer::BusinessLayer() = default;

BusinessLayer::~BusinessLayer() {
    stop();
}

bool BusinessLayer::init(IDeviceService& device, IReportSink& sink,
                         MessageBuffer<BusinessCommand>& bizQueue) {
    sink_ = &sink;
    bizQueue_ = &bizQueue;

    auto& cfg = ConfigManager::instance();
    boxType_ = cfg.box().boxType;
    boxNo_   = cfg.box().boxNo;

    // 与 DataLayer 用同一套白名单。这里如果写成 "不是驿公里就当海天"，
    // 配置写错时会出现"PLC 走 A、服务实现走 B"的错配，日志里完全看不出来。
    if (boxType_ == "haiTian") {
        plcId_   = "fx_plc_01";
        service_ = std::make_unique<HaiTianWashCarService>(device, sink);
        LOG_INFO("[Business] 机型匹配: boxType=haiTian -> HaiTianWashCarService");
    } else if (boxType_ == "yiGongLi") {
        plcId_   = cfg.plcTcp().plcId;
        service_ = std::make_unique<YiGongLiWashCarService>(device, sink);
        LOG_INFO("[Business] 机型匹配: boxType=yiGongLi -> YiGongLiWashCarService, plcId=" + plcId_);
    } else {
        LOG_ERROR("[Business] 机型不匹配: box.boxType=\"" + boxType_
                  + "\" 不是支持的值 (haiTian / yiGongLi)");
        return false;
    }

    reportScheduler_ = std::make_unique<ReportScheduler>(*service_);
    return true;
}

void BusinessLayer::start() {
    if (reportScheduler_) {
        reportScheduler_->start();
    }
    if (bizQueue_ == nullptr) {
        LOG_ERROR("[Business] 业务队列未注入，命令分派线程不启动");
        return;
    }
    running_.store(true);
    if (!dispatchThread_.start("biz-dispatch", [this]() { dispatchLoop(); })) {
        LOG_ERROR("[Business] 线程预算不足，命令分派线程未启动（收到的命令不会被处理）");
        running_.store(false);
    }
}

void BusinessLayer::stop() {
    // 顺序：先停分派线程（不再有新命令进来），再停上报调度，最后停洗车监控。
    if (running_.exchange(false)) {
        if (bizQueue_ != nullptr) {
            bizQueue_->shutdown();   // 唤醒 popWait
        }
        if (dispatchThread_.joinable()) {
            dispatchThread_.join();
        }
    }
    if (reportScheduler_) {
        reportScheduler_->stop();
    }
    if (service_) {
        service_->stopWashCarStatusMonitor();
    }
}

void BusinessLayer::dispatchLoop() {
    constexpr int kTickMs = 50;
    while (running_.load()) {
        BusinessCommand cmd;
        if (!bizQueue_->popWait(cmd, kTickMs)) {
            continue;
        }
        if (!running_.load()) {
            break;
        }

        // onCommand 内部已按命令类型分派；这里再兜一层异常，
        // 保证一条坏命令不会打死分派线程（后面所有命令都收不到）。
        try {
            onCommand(cmd);
        } catch (const std::exception& e) {
            LOG_ERROR(std::string("[Business] onCommand exception: ") + e.what());
        } catch (...) {
            LOG_ERROR("[Business] onCommand unknown exception");
        }
    }
    LOG_INFO("[Business] 命令分派线程退出, dropped(biz)="
             + std::to_string(bizQueue_ ? bizQueue_->droppedCount() : 0));
}

WashCarOperation BusinessLayer::makeOperation(const BusinessCommand& cmd) const {
    WashCarOperation op(cmd.raw);
    op.setReqSource("mqtt");
    if (op.getBoxNo().empty()) {
        op.setBoxNo(boxNo_);
    }
    if (op.getPlcId().empty()) {
        op.setPlcId(plcId_);
    }
    return op;
}

void BusinessLayer::replySuccess() {
    if (sink_ == nullptr) {
        return;
    }
    nlohmann::json msg;
    msg["message"] = "success";
    msg["time"] = washcar::timeutil::nowSeconds();
    sink_->push(Report{ReportKind::COMMAND_REPLY, msg.dump()});
}

void BusinessLayer::handleRead(const BusinessCommand& cmd) {
    const auto result = service_->readPoints(cmd.points);
    if (result.getCode() != ErrorCode::Code::SUCCESS) {
        // 采不到就静默，与"只有成功才回执"的约定一致
        LOG_ERROR("[Business] readPoints failed: " + result.getMessage() + ", no readdata sent");
        return;
    }

    nlohmann::json msg = nlohmann::json::object();
    for (const auto& [key, value] : result.getStatus()) {
        msg[key] = value;
    }
    msg["boxNo"] = boxNo_;
    msg["time"] = washcar::timeutil::nowSeconds();

    LOG_INFO("[Business] readdata ready, pointCount=" + std::to_string(result.getStatus().size()));
    if (sink_ != nullptr) {
        sink_->push(Report{ReportKind::READDATA, msg.dump()});
    }
}

void BusinessLayer::onCommand(const BusinessCommand& cmd) {
    if (!service_) {
        return;
    }
    try {
        if (cmd.type == CommandType::READ) {
            handleRead(cmd);
            return;
        }
        dispatch(cmd);
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("[Business] onCommand exception: ") + e.what());
    } catch (...) {
        LOG_ERROR("[Business] onCommand unknown exception");
    }
}

void BusinessLayer::dispatch(const BusinessCommand& cmd) {
    const WashCarOperation op = makeOperation(cmd);
    WashCarOperationResult result;

    switch (cmd.type) {
    case CommandType::START:
        result = service_->startWashCar(op);
        if (result.getCode() == ErrorCode::Code::SUCCESS) {
            // 启动成功后拉起进度上报线程（D142 → monitor）
            service_->startWashCarStatusMonitor(op);
            replySuccess();
        }
        break;

    case CommandType::STOP:
        result = service_->stopWashCar(op);
        break;

    case CommandType::RESET:
        result = service_->resetWashCar(op);
        break;

    case CommandType::ABORT:
        result = service_->abortWashCar(op);
        break;

    case CommandType::STOP_WORK:
        result = service_->stopWork(op);
        break;

    case CommandType::STOP_SERVICE:
        result = service_->stopService(op);
        break;

    case CommandType::RESET_SAFE:
        result = service_->resetSafe(op);
        break;

    case CommandType::POINT_ON:
        result = service_->pointOn(op);
        break;

    case CommandType::POINT_OFF:
        result = service_->pointOff(op);
        break;

    case CommandType::POINT_STATUS:
        result = service_->pointStatus(op);
        break;

    case CommandType::CLEAR_ALARM:
        result = service_->clearAlarm(op);
        break;

    case CommandType::CHANGE_MODE:
        if (service_->changeMode(op)) {
            replySuccess();
        } else {
            LOG_WARNING("[Business] changeMode rejected (only allowed from http source)");
        }
        return;

    case CommandType::REBOOT:
        // 繁易协议约定：盒子只识别并记录 reboot，不自己退出。
        // 现场确实需要远程重启时，把 mqtt.rebootEnabled 打开（默认 false）：
        // 这里只发 SIGTERM，交给 main 的退出流程优雅收尾，再由 systemd/守护脚本拉起。
        // 不直接调 exit()，否则线程不会被 join、日志可能丢。
        if (ConfigManager::instance().mqtt().rebootEnabled) {
            LOG_WARNING("[Business] reboot requested, mqtt.rebootEnabled=true, shutting down");
            std::raise(SIGTERM);
        } else {
            LOG_WARNING("[Business] reboot requested, recorded only (mqtt.rebootEnabled=false)");
        }
        return;

    case CommandType::READ:
    case CommandType::UNKNOWN:
    default:
        LOG_WARNING(std::string("[Business] unhandled command: ") + toString(cmd.type));
        return;
    }

    if (result.getCode() == ErrorCode::Code::SUCCESS) {
        replySuccess();
    } else {
        // 按协议约定，失败不回执，只留日志供现场排查
        LOG_WARNING(std::string("[Business] command failed: ") + toString(cmd.type)
                    + ", msg=" + result.getMessage());
    }
}
