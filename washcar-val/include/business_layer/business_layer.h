#pragma once

// 业务层聚合根。
//
// 职责：
//   - 按机型造洗车服务（海天 / 驿公里）
//   - **自己起一条线程**从业务消息队列取消息并处理（onCommand）
//   - 驱动三路主动上报（ReportScheduler）
//
// 业务层不直接被表现层调用：表现层只往业务队列里放 BusinessCommand，
// 本层的分派线程按自己的节奏取。这样"收"和"处理"彻底解耦——
// 一条驿公里启动命令要占 3 秒，期间后面排队的命令在队列里等着，不会堵住 MQTT。
//
// main 只调 init / start / stop。

#include "common/buffer/message_buffer.h"
#include "common/message.h"
#include "common/thread/thread_manager.h"
#include "business_layer/washcar/washcar_service.h"
#include "common/message.h"

#include <atomic>
#include <memory>
#include <string>

class IDeviceService;
class IWashCarService;
class ReportScheduler;

class BusinessLayer {
public:
    BusinessLayer();
    ~BusinessLayer();

    BusinessLayer(const BusinessLayer&) = delete;
    BusinessLayer& operator=(const BusinessLayer&) = delete;

    /// device / sink / bizQueue 的生命周期需长于本对象
    bool init(IDeviceService& device, IReportSink& sink,
              MessageBuffer<BusinessCommand>& bizQueue);

    /// 起分派线程 + 上报调度线程
    void start();
    void stop();

    /// 处理一条业务消息。解析失败（type=UNKNOWN）由解析层拦掉，不进这里。
    /// 分派线程调它；单元测试也可以直接调。
    void onCommand(const BusinessCommand& cmd);

private:
    /// 命令参数补全：reqSource / boxNo / plcId 兜底
    WashCarOperation makeOperation(const BusinessCommand& cmd) const;

    /// 命令成功回执
    void replySuccess();

    /// READ：采样后组 readdata 报文
    void handleRead(const BusinessCommand& cmd);

    /// 按机型分发到 IWashCarService 的对应方法，成功回执
    void dispatch(const BusinessCommand& cmd);

    std::unique_ptr<IWashCarService> service_;
    std::unique_ptr<ReportScheduler> reportScheduler_;

    /// 从业务队列取消息的循环
    void dispatchLoop();

    MessageBuffer<BusinessCommand>* bizQueue_{nullptr};
    ManagedThread dispatchThread_;
    std::atomic<bool> running_{false};

    IReportSink* sink_{nullptr};
    std::string boxType_;
    std::string boxNo_;
    std::string plcId_;
};
