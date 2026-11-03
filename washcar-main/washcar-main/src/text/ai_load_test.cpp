// AI 负载测试: 三个模型(yolov8 + 车牌检测 + 车牌识别)常驻, 按生产节奏连续检测,
// 同时跑一个模拟业务任务(定时器/轮询), 看 AI 负载下业务任务还稳不稳。
//
// 用法: ./ai_load_test [秒数] [图片路径]
#include "business_layer/detection/detection.h"
#include "common/ai/ai_model.h"
#include "common/config/config_manager.h"
#include "common/log/log_manager.h"
#include "data_layer/ai/ai_dao.h"
#include "data_layer/camera/camera_real_time_frame.h"
#include "image_utils.h"

#include <unistd.h>

#include <filesystem>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono;

static double readCpuSeconds() {
    std::ifstream f("/proc/self/stat");
    std::string line;
    std::getline(f, line);
    // utime(14) stime(15), 按空格切
    std::istringstream ss(line);
    std::string tok;
    long utime = 0, stime = 0;
    int idx = 0;
    while (ss >> tok) {
        idx++;
        if (idx == 14) utime = std::stol(tok);
        if (idx == 15) { stime = std::stol(tok); break; }
    }
    return (double)(utime + stime) / (double)sysconf(_SC_CLK_TCK);
}

static long readRssKb() {
    std::ifstream f("/proc/self/status");
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            return std::stol(line.substr(6));
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    int runSeconds = argc > 1 ? atoi(argv[1]) : 30;
    std::string imgPath = argc > 2 ? argv[2] : "plate_new.jpg";
    bool withPlate = argc > 3 ? atoi(argv[3]) != 0 : true;   // 对照组: 只挂 yolov8

    // 跟 main 一样: chdir 到 exe 目录, 相对路径(./data)才落在 exe 同级
    std::filesystem::path exeDir;
    try {
        exeDir = std::filesystem::absolute(argv[0]).parent_path();
    } catch (...) {
        exeDir = std::filesystem::current_path();
    }
    std::filesystem::current_path(exeDir);
    // 加载配置(文件不存在走默认值, 目的是拿到 storageRoot=./data)
    {
        std::string cfgErr;
        ConfigManager::instance().load("", cfgErr);
    }
    int detectIntervalMs = 2000;   // 生产高频周期 2s

    auto queue = std::make_shared<BlockingQueue>();
    auto logger = std::make_shared<AsyncLogger>(queue);
    logger->addSink(std::make_shared<ConsoleSink>());
    logger->setLogLevel(LogLevel::INFO);
    logger->start();
    LoggerManager::instance().registerLogger("work", logger);

    image_buffer_t img = {};
    if (read_image(imgPath.c_str(), &img) != 0 || !img.virt_addr) {
        std::printf("[LOAD] read_image failed: %s\n", imgPath.c_str());
        return 1;
    }
    std::vector<uint8_t> rgb(img.virt_addr, img.virt_addr + img.size);
    int imgW = img.width, imgH = img.height;
    free(img.virt_addr);

    washcar::cfg::AiConfig aiCfg;
    aiCfg.aiEnabled = true;
    aiCfg.idleIntervalMs = detectIntervalMs;
    aiCfg.workingIntervalMs = detectIntervalMs;
    // 验证存储上限: 环境变量可压小上限, 观察清理是否生效
    if (const char* envMax = getenv("WASHCAR_MAX_RESULTS")) {
        aiCfg.maxDetectionResults = atoi(envMax);
    }
    if (const char* envMb = getenv("WASHCAR_MAX_IMAGE_MB")) {
        aiCfg.maxAlarmImageMb = atoi(envMb);
    }
    washcar::cfg::AiModelConfig yolo;
    yolo.modelId = "yolov8";
    yolo.modelKind = "yolov8";
    yolo.modelPaths = {"lib/rknn/models/yolov8.rknn"};
    yolo.classIds = {0, 2, 3, 1};
    yolo.classTypes = {"PERSON_DETECTED", "CAR_PRESENT", "MOTORCYCLE", "BICYCLE"};
    aiCfg.modelConfigs.push_back(yolo);
    if (withPlate) {
        washcar::cfg::AiModelConfig plate;
        plate.modelId = "plate";
        plate.modelKind = "plate";
        plate.modelPaths = {"lib/rknn/models/ppocrv4_det.rknn", "lib/rknn/models/lprnet.rknn"};
        plate.classIds = {0};
        plate.classTypes = {"LICENSE_PLATE"};
        plate.roi = {690, 500, 220, 200};
        aiCfg.modelConfigs.push_back(plate);
        aiCfg.regions.push_back({"service", {"yolov8"}, {"yolov8", "plate"}});
    } else {
        aiCfg.regions.push_back({"service", {"yolov8"}, {"yolov8"}});
    }

    DetectionResultCache cache;
    RealTimeFrameCache frameCache;
    DatabaseManager& db = DatabaseManager::instance();
    // 初始化数据目录(不初始化的话 ai.db 打不开, 检测结果不会落库)
    if (!db.init("./data")) {
        std::printf("[LOAD] DatabaseManager init failed\n");
        return 1;
    }
    ModuleDao dao(db);
    DetectionService svc(dao, frameCache, cache, aiCfg);

    svc.startDetection();

    // 模拟业务任务: 5ms 一次的轻量轮询(PLC 采集/状态刷新那种), 记录抖动
    std::atomic<bool> stop{false};
    std::vector<double> latencies;
    std::thread biz([&]{
        latencies.reserve(6000);
        auto next = steady_clock::now();
        while (!stop.load()) {
            next += milliseconds(5);
            auto t0 = steady_clock::now();
            volatile int64_t dummy = 0;
            for (int i = 0; i < 50; ++i) dummy += i;   // 一点真实计算量
            auto t1 = steady_clock::now();
            latencies.push_back(duration<double, std::milli>(t1 - t0).count());
            std::this_thread::sleep_until(next);
        }
    });

    // 主线程: 按周期喂帧 + 触发检测, 采样资源占用
    double cpuStart = readCpuSeconds();
    long rssPeak = 0;
    int rounds = 0;
    auto tStart = steady_clock::now();
    auto nextFrame = tStart;
    while (duration_cast<std::chrono::seconds>(steady_clock::now() - tStart).count() < runSeconds) {
        nextFrame += milliseconds(detectIntervalMs);
        FrameData fd;
        fd.width = imgW;
        fd.height = imgH;
        auto buf = new uint8_t[rgb.size()];
        std::copy(rgb.begin(), rgb.end(), buf);
        fd.data = std::shared_ptr<uint8_t>(buf, [](uint8_t* p) { delete[] p; });
        fd.timestamp_ = steady_clock::now().time_since_epoch().count() / 1000000;
        frameCache.updateCameraRealTimeFrame("service", fd);

        svc.detectRegion("service");
        rounds++;
        rssPeak = std::max(rssPeak, readRssKb());
        std::this_thread::sleep_until(nextFrame);
    }
    stop.store(true);
    biz.join();
    svc.stopDetection();

    double cpuEnd = readCpuSeconds();
    double wall = duration_cast<std::chrono::seconds>(steady_clock::now() - tStart).count();
    double cpuPct = (cpuEnd - cpuStart) / wall * 100.0;

    std::sort(latencies.begin(), latencies.end());
    auto pct = [&](double p) {
        if (latencies.empty()) return 0.0;
        size_t i = (size_t)(p * (latencies.size() - 1));
        return latencies[i];
    };
    double sum = 0;
    for (double v : latencies) sum += v;
    double avg = latencies.empty() ? 0 : sum / latencies.size();

    std::printf("\n===== AI 负载测试结果 (%d 秒) =====\n", runSeconds);
    std::printf("  模型: yolov8 + 车牌检测 + 车牌识别 (3 个 rknn 常驻)\n");
    std::printf("  检测轮次: %d 轮 (每 %d ms 一轮)\n", rounds, detectIntervalMs);
    std::printf("  进程 CPU 占用: %.1f%%  (4 核, 100%% = 满载)\n", cpuPct);
    std::printf("  进程 RSS 峰值: %.1f MB\n", rssPeak / 1024.0);
    std::printf("  模拟业务任务(5ms 周期) 耗时: 均值 %.3f ms | P50 %.3f | P99 %.3f | 最大 %.3f\n",
                avg, pct(0.5), pct(0.99), latencies.empty() ? 0.0 : latencies.back());
    std::printf("==========================================\n");
    return 0;
}
