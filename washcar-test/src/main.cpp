// =============================================================================
// washcar-test —— 洗车机主控
//
// 这个文件只做装配，不含任何业务、协议、线程、文件、信号、配置解析的细节。
// 全篇只有五段：
//
//   1. 公共设施初始化（程序目录 + 读 config.json + 日志 + 线程预算 + 指标监控 + 退出信号）
//   2. 公共设施启动（日志线程 + 指标采样线程）
//   3. 中间传输 buffer（原始报文 / 业务消息 / 上行报文 三个队列）
//   4. 三层的 init / start
//   5. 等退出信号 -> 三层 stop -> 公共设施 stop
//
// 完整链路（下行两段队列、上行一段）：
//   MQTT 收包 ──rawBuffer──► 下行泵(解析) ──bizBuffer──► 业务分派线程
//                                                        ├─► DataLayer(PLC 读写)
//                                                        └─► upBuffer ──► 上行泵 ──► MQTT
// =============================================================================

#include "business_layer/business_layer.h"
#include "common/common.h"
#include "common/buffer/message_buffer.h"
#include "common/buffer/uplink_buffer.h"
#include "common/config/config_manager.h"
#include "common/message.h"
#include "data_layer/data_layer.h"
#include "presentation/presentation_layer.h"

#include <iostream>

int main(int argc, char** argv) {
    std::cout << "=============================================" << std::endl;
    std::cout << " washcar-test 洗车机主控" << std::endl;
    std::cout << "=============================================" << std::endl;

    auto& app = Common::instance();

    // ---------- 1. 公共设施初始化 ----------
    if (!app.init(argc > 0 ? argv[0] : "main")) {
        return -1;
    }

    // ---------- 2. 公共设施启动（日志线程 + 指标采样） ----------
    if (!app.start()) {
        return -1;
    }

    // ---------- 3. 中间传输 buffer ----------
    // 容量来自 config.json 的 queueCapacity 段（下面简称 queuesCfg）（改了重启生效）；填 0 用默认值。
    //   原始报文：MQTT 回调写入，容量给大一点（突发不该压到解析上）
    //   业务消息：解析结果，命令不会堆，给中等容量
    //   上行报文：monitor/readdata 同类只留最新，所以不需要很大
    const auto& queuesCfg = ConfigManager::instance().queueCapacity();
    MessageBuffer<RawMessage> rawBuffer;
    MessageBuffer<BusinessCommand> bizBuffer;
    UplinkBuffer upBuffer;
    rawBuffer.init(queuesCfg.downCapacity > 0 ? static_cast<std::size_t>(queuesCfg.downCapacity) : 256);
    bizBuffer.init(queuesCfg.downCapacity > 0 ? static_cast<std::size_t>(queuesCfg.downCapacity) : 256);
    upBuffer.init(queuesCfg.upCapacity > 0 ? static_cast<std::size_t>(queuesCfg.upCapacity) : 128);

    // ---------- 4. 三层 ----------
    DataLayer         dataLayer;
    BusinessLayer     businessLayer;
    PresentationLayer presentationLayer;

    // 半途失败也要按相反顺序收尾：已经起来的层要 join 线程，
    // 日志要 flush，否则退出时最后几条日志（往往正是失败原因）会丢。
    bool dataStarted = false;
    bool businessStarted = false;
    bool presentationStarted = false;

    auto fail = [&](const char* what) {
        std::cerr << ">>> 错误: " << what << std::endl;
        if (presentationStarted) presentationLayer.stop();
        if (businessStarted)     businessLayer.stop();
        if (dataStarted)         dataLayer.stop();
        app.stop();
        return -1;
    };

    std::cout << "\n>>> 初始化数据层" << std::endl;
    if (!dataLayer.init()) {
        return fail("数据层初始化失败");
    }

    std::cout << "\n>>> 初始化业务层" << std::endl;
    if (!businessLayer.init(dataLayer.device(), upBuffer, bizBuffer)) {
        return fail("业务层初始化失败");
    }

    std::cout << "\n>>> 初始化表现层（MQTT + 解析 + 队列）" << std::endl;
    if (!presentationLayer.init(rawBuffer, bizBuffer, upBuffer)) {
        return fail("表现层初始化失败");
    }

    // 业务层要先起（分派线程就位），再起表现层（开始往里投命令）。
    // 反了的话，上电瞬间收到的命令会在队列里等一会儿，虽然不会丢，但顺序不直观。
    dataLayer.start();
    dataStarted = true;
    businessLayer.start();
    businessStarted = true;
    if (!presentationLayer.start()) {
        return fail("表现层启动失败");
    }
    presentationStarted = true;

    std::cout << "\n>>> 系统启动完成，按 Ctrl+C 停止" << std::endl;
    std::cout << "=============================================" << std::endl;

    // ---------- 5. 等退出信号 ----------
    app.waitForExit();

    std::cout << "\n>>> 正在关闭..." << std::endl;
    // 顺序有意义：先断上游（不再产生报文），再停业务（不再产生上报），
    // 最后停采集（不再写缓存）。日志放最后，好让上面每一步的收尾日志都落盘。
    presentationLayer.stop();
    businessLayer.stop();
    dataLayer.stop();
    app.stop();

    std::cout << ">>> 已停止" << std::endl;
    return 0;
}
