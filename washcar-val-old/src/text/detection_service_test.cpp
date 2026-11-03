// detection_service_test.cpp — 检测服务 v6 重构的 TDD 验证
// 覆盖: 业务类别名解析 / DetectionResultCache / ai 配置解析 / 构造生命周期 /
//       构造失败抛异常 / 帧缓冲(RealTimeFrameCache) / 检测接口不崩
// 用法: 编译后直接跑 ./detection_service_test
// 退出码: 0 = 全过, 非 0 = 失败用例数
#include "business_layer/detection/detection.h"
#include "business_layer/detection/detection_result_cache.h"
#include "business_layer/device/real_time_frame_cache.h"
#include "common/config/config_manager.h"
#include "common/log/log_manager.h"
#include "image_utils.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

using washcar::cfg::AiConfig;
using washcar::cfg::AiModelConfig;
using washcar::cfg::AiRegionBinding;

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, desc) do {                                              \
    if (cond) { ++g_pass; std::cout << "  [PASS] " << desc << std::endl; }   \
    else      { ++g_fail; std::cout << "  [FAIL] " << desc << std::endl; }   \
} while (0)

static void writeTempJson(const std::string& path, const std::string& body) {
    std::ofstream out(path);
    out << body;
}

// 造一帧假数据(3x2 RGB), 只验证缓冲链路, 不跑推理
static FrameData makeFrame() {
    FrameData fd;
    fd.width = 3;
    fd.height = 2;
    auto buf = new uint8_t[3 * 2 * 3];
    for (int i = 0; i < 18; ++i) buf[i] = static_cast<uint8_t>(i);
    fd.data = std::shared_ptr<uint8_t>(buf, [](uint8_t* p) { delete[] p; });
    fd.timestamp_ = 1000;
    return fd;
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    // 初始化最小日志(避免 LOG_* 拿到 null logger)
    auto queue = std::make_shared<BlockingQueue>();
    auto logger = std::make_shared<AsyncLogger>(queue);
    logger->addSink(std::make_shared<ConsoleSink>());
    logger->setLogLevel(LogLevel::WARNING);
    logger->start();
    LoggerManager::instance().registerLogger("work", logger);

    // 初始化数据目录(不初始化的话 ai.db 打不开, 检测结果不会落库)
    // 跟 main 一样 chdir 到 exe 目录, 相对路径(./data)才落在 exe 同级
    {
        std::filesystem::path exeDir;
        try {
            exeDir = std::filesystem::absolute(argv[0]).parent_path();
        } catch (...) {
            exeDir = std::filesystem::current_path();
        }
        std::filesystem::current_path(exeDir);
        std::string cfgErr;
        ConfigManager::instance().load("", cfgErr);
    }
    if (!DatabaseManager::instance().init("./data")) {
        std::cout << "  [FAIL] DatabaseManager init failed" << std::endl;
    }

    std::cout << "========== detection_service_test ==========" << std::endl;

    // ---------- 用例 1: 业务类别名 <-> 枚举 ----------
    {
        DetectionResult::Type t;
        CHECK(typeFromString("CAR_PRESENT", t) && t == DetectionResult::Type::CAR_PRESENT,
              "1. typeFromString('CAR_PRESENT') ok");
        CHECK(typeFromString("PERSON_DETECTED", t) && t == DetectionResult::Type::PERSON_DETECTED,
              "1. typeFromString('PERSON_DETECTED') ok");
        CHECK(!typeFromString("UNKNOWN", t),
              "1. typeFromString('UNKNOWN') returns false");
        CHECK(typeToString(DetectionResult::Type::DOOR_OPEN) == "DOOR_OPEN",
              "1. typeToString(DOOR_OPEN) round-trip");
    }

    // ---------- 用例 2: DetectionResultCache 写入/读取/同源覆盖/clear ----------
    {
        DetectionResultCache cache;
        DetectionRecord r1;
        r1.seq = 1; r1.cameraId = "service"; r1.callTime = 10; r1.inferTime = 20;
        DetectionRecord r2 = r1;
        r2.seq = 2; r2.callTime = 30; r2.inferTime = 40;

        CHECK(cache.read("service").seq == 0, "2. read before write -> seq==0 (无效)");

        cache.write("service", r1);
        CHECK(cache.read("service").seq == 1, "2. write -> read seq=1");

        cache.write("service", r2);   // 同源新结果覆盖旧结果
        CHECK(cache.read("service").seq == 2, "2. same-source new result overwrites old");

        CHECK(cache.read("queue").seq == 0, "2. other region unaffected");

        cache.clear();
        CHECK(cache.read("service").seq == 0, "2. clear -> seq==0");
    }

    // ---------- 用例 3: ConfigManager 解析 ai 配置(模型定义/区域-模型对/频率) ----------
    {
        const std::string path = "/tmp/__washcar_ai_cfg__.json";
        writeTempJson(path, R"({
            "box":   {"boxNo": "abc"},
            "mqtt":  {"brokerIp": "1.2.3.4"},
            "ai": {
                "idleIntervalMs": 5000,
                "workingIntervalMs": 1000,
                "exitWorkingEmptyTimes": 10,
                "modelConfigs": [
                    {"modelId": "vehicle", "modelPath": "/opt/m.rknn", "classIds": [0], "classTypes": ["CAR_PRESENT"]},
                    {"modelId": "person",  "modelPath": "/opt/m.rknn", "classIds": [1], "classTypes": ["PERSON_DETECTED"]}
                ],
                "regions": [
                    {"regionId": "service", "idleModelIds": ["vehicle", "person"], "workingModelIds": ["vehicle", "person"]}
                ]
            }
        })");
        std::string err;
        bool ok = ConfigManager::instance().load(path, err);
        CHECK(ok, "3. ai config JSON loads");
        const auto& ai = ConfigManager::instance().ai();
        CHECK(ai.idleIntervalMs == 5000 && ai.workingIntervalMs == 1000
              && ai.exitWorkingEmptyTimes == 10, "3. frequencies parsed from JSON");
        CHECK(ai.modelConfigs.size() == 2 && ai.modelConfigs[0].modelId == "vehicle"
              && ai.modelConfigs[0].classIds[0] == 0
              && ai.modelConfigs[0].classTypes[0] == "CAR_PRESENT",
              "3. modelConfigs parsed from JSON");
        CHECK(ai.regions.size() == 1 && ai.regions[0].regionId == "service"
              && ai.regions[0].idleModelIds.size() == 2,
              "3. regions parsed from JSON");
        std::remove(path.c_str());
    }

    // ---------- 用例 4: 不接 AI(aiEnabled=false) 构造 + 起停生命周期 ----------
    {
        AiConfig aiCfg;   // 默认: 频率 6000/2000/20, 无模型无区域
        DetectionResultCache cache;
        RealTimeFrameCache frameCache;
        DatabaseManager& db = DatabaseManager::instance();
        ModuleDao dao(db);
        try {
            DetectionService svc(dao, frameCache, cache, aiCfg);
            svc.startDetection();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            svc.stopDetection();
            CHECK(true, "4. empty modelConfigs (AI off) construct + start/stop ok");
        } catch (const std::exception& e) {
            CHECK(false, std::string("4. empty modelConfigs should NOT throw: ") + e.what());
        }
    }

    // ---------- 用例 5: 区域引用未定义模型 -> 构造抛异常 ----------
    {
        AiConfig aiCfg;
        aiCfg.aiEnabled = true;   // 走加载/建区域逻辑才会抛
        aiCfg.regions.push_back({"service", {"vehicle"}, {"vehicle"}});   // 模型未配置
        DetectionResultCache cache;
        RealTimeFrameCache frameCache;
        DatabaseManager& db = DatabaseManager::instance();
        ModuleDao dao(db);
        bool threw = false;
        try {
            DetectionService svc(dao, frameCache, cache, aiCfg);
        } catch (const std::exception&) {
            threw = true;
        }
        CHECK(threw, "5. region referencing undefined model -> constructor throws");
    }

    // ---------- 用例 6: modelPaths 空 -> 模型被跳过, 不拖垮主程序 ----------
    {
        AiConfig aiCfg;
        aiCfg.aiEnabled = true;   // 走加载逻辑才会校验 modelPaths
        AiModelConfig mc;
        mc.modelId = "vehicle";
        mc.classIds = {0};
        mc.classTypes = {"CAR_PRESENT"};
        aiCfg.modelConfigs.push_back(mc);   // modelPaths 留空
        DetectionResultCache cache;
        RealTimeFrameCache frameCache;
        DatabaseManager& db = DatabaseManager::instance();
        ModuleDao dao(db);
        bool threw = false;
        try {
            DetectionService svc(dao, frameCache, cache, aiCfg);
        } catch (const std::exception&) {
            threw = true;
        }
        CHECK(!threw, "6. empty modelPaths -> 该模型跳过, 构造不抛异常");
    }

    // ---------- 用例 7: 帧缓冲(RealTimeFrameCache)写入/读取/同源覆盖 ----------
    {
        RealTimeFrameCache frameCache;
        frameCache.updateCameraRealTimeFrame("service", makeFrame());
        frameCache.updateCameraRealTimeFrame("service", makeFrame());   // 同源覆盖
        CameraRealTimeFrame f = frameCache.getCameraRealTimeFrame(CameraInfo("service"));
        CHECK(f.isValid(), "7. frame cache write+read valid");
        CameraRealTimeFrame g = frameCache.getCameraRealTimeFrame(CameraInfo("queue"));
        CHECK(!g.isValid(), "7. un-written camera -> invalid frame");
    }

    // ---------- 用例 8: 检测服务(无模型) 新接口调用不崩 ----------
    {
        AiConfig aiCfg;   // 无模型无区域
        DetectionResultCache cache;
        RealTimeFrameCache frameCache;
        DatabaseManager& db = DatabaseManager::instance();
        ModuleDao dao(db);
        DetectionService svc(dao, frameCache, cache, aiCfg);
        svc.startDetection();
        svc.detectRegion("service");          // 未注册区域 -> 警告, 不崩
        svc.detectAllRegions();
        svc.increaseFrequency("service");
        svc.decreaseFrequency("service");
        svc.stopDetection();
        CHECK(true, "8. detectRegion/detectAllRegions/frequency calls don't crash");
    }

    // ---------- 用例 9: 真实 yolov8 模型(模型/测试图存在时; 验证加载+推理+结果链路) ----------
    {
        std::string modelPath, imgPath;
        for (const auto& c : {"lib/rknn/models/yolov8.rknn",
                              "../../../../src/lib/rknn/models/yolov8.rknn",
                              "/home/ztl/models/yolov8.rknn"}) {
            if (std::filesystem::exists(c)) { modelPath = c; break; }
        }
        for (const auto& c : {"lib/rknn/models/bus.jpg",
                              "../../../../src/lib/rknn/models/bus.jpg",
                              "/home/ztl/models/bus.jpg"}) {
            if (std::filesystem::exists(c)) { imgPath = c; break; }
        }
        if (modelPath.empty() || imgPath.empty()) {
            std::cout << "  [SKIP] 9. 真实模型/测试图不存在, 跳过 (model=" << modelPath
                      << ", img=" << imgPath << ")" << std::endl;
        } else {
            AiConfig aiCfg;
            aiCfg.aiEnabled = true;   // 真实模型加载
            aiCfg.idleIntervalMs = 50;    // 加速: 检测到期快, 不等真实 6s
            aiCfg.workingIntervalMs = 50;
            AiModelConfig mc;
            mc.modelId = "yolov8";
            mc.modelPaths = {modelPath};
            mc.classIds = {0};
            mc.classTypes = {"PERSON_DETECTED"};
            aiCfg.modelConfigs.push_back(mc);
            aiCfg.regions.push_back({"service", {"yolov8"}, {"yolov8"}});
            DetectionResultCache cache;
            RealTimeFrameCache frameCache;
            DatabaseManager& db = DatabaseManager::instance();
            ModuleDao dao(db);
            try {
                DetectionService svc(dao, frameCache, cache, aiCfg);   // 构造 = 真实模型加载
                svc.startDetection();

                image_buffer_t img = {};
                if (read_image(imgPath.c_str(), &img) != 0 || !img.virt_addr) {
                    CHECK(false, "9. read_image failed");
                    svc.stopDetection();
                } else {
                    FrameData fd;
                    fd.width = img.width;
                    fd.height = img.height;
                    auto buf = new uint8_t[img.size];
                    std::copy(img.virt_addr, img.virt_addr + img.size, buf);
                    fd.data = std::shared_ptr<uint8_t>(buf, [](uint8_t* p) { delete[] p; });
                    fd.timestamp_ = 1000;
                    free(img.virt_addr);   // read_image 内 malloc 的 RGB 缓冲
                    frameCache.updateCameraRealTimeFrame("service", fd);

                    svc.detectRegion("service");
                    std::this_thread::sleep_for(std::chrono::milliseconds(800));  // 等模型线程推理+提交
                    auto rec = cache.read("service");

                    bool hasPerson = false;
                    for (const auto& it : rec.result.items) {
                        if (it.type == DetectionResult::Type::PERSON_DETECTED && it.detected) {
                            hasPerson = true;
                            std::cout << "  [INFO] 9. person detected, conf=" << it.confidence << std::endl;
                        }
                    }
                    CHECK(rec.seq > 0, "9. real model round produced a result record");
                    CHECK(hasPerson, "9. bus.jpg 检出 person (COCO class 0)");
                    svc.stopDetection();
                }
            } catch (const std::exception& e) {
                CHECK(false, std::string("9. real model load/infer: ") + e.what());
            }
        }
    }

    // ---------- 用例 10: 车牌模型(检测+识别两级串联, 用现场帧验证) ----------
    {
        std::string recModel, detModel, imgPath;
        for (const auto& c : {"lib/rknn/models/lprnet.rknn",
                              "/home/ztl/washcar/aitest/lib/rknn/models/lprnet.rknn"}) {
            if (std::filesystem::exists(c)) { recModel = c; break; }
        }
        for (const auto& c : {"lib/rknn/models/ppocrv4_det.rknn",
                              "/home/ztl/washcar/aitest/lib/rknn/models/ppocrv4_det.rknn"}) {
            if (std::filesystem::exists(c)) { detModel = c; break; }
        }
        for (const auto& c : {"plate_new.jpg",
                              "plate_new.png",
                              "/home/ztl/washcar/aitest/plate_new.jpg"}) {
            if (std::filesystem::exists(c)) { imgPath = c; break; }
        }
        if (recModel.empty() || detModel.empty() || imgPath.empty()) {
            std::cout << "  [SKIP] 10. 车牌模型/测试图不存在, 跳过" << std::endl;
        } else {
            AiConfig aiCfg;
            aiCfg.aiEnabled = true;
            aiCfg.idleIntervalMs = 50;
            aiCfg.workingIntervalMs = 50;
            AiModelConfig mc;
            mc.modelId = "plate";
            mc.modelPaths = {detModel, recModel};   // 顺序: [检测, 识别]
            mc.classIds = {0};
            mc.classTypes = {"LICENSE_PLATE"};
            mc.modelKind = "plate";
            mc.roi = {690, 500, 220, 200};    // 出口侧车牌在画面里的位置
            aiCfg.modelConfigs.push_back(mc);
            aiCfg.regions.push_back({"service", {"plate"}, {"plate"}});
            DetectionResultCache cache;
            RealTimeFrameCache frameCache;
            DatabaseManager& db = DatabaseManager::instance();
            ModuleDao dao(db);
            try {
                DetectionService svc(dao, frameCache, cache, aiCfg);   // 构造 = 两个模型都加载
                svc.startDetection();

                image_buffer_t img = {};
                if (read_image(imgPath.c_str(), &img) != 0 || !img.virt_addr) {
                    CHECK(false, "10. read_image failed");
                } else {
                    FrameData fd;
                    fd.width = img.width;
                    fd.height = img.height;
                    auto buf = new uint8_t[img.size];
                    std::copy(img.virt_addr, img.virt_addr + img.size, buf);
                    fd.data = std::shared_ptr<uint8_t>(buf, [](uint8_t* p) { delete[] p; });
                    fd.timestamp_ = 2000;
                    free(img.virt_addr);
                    frameCache.updateCameraRealTimeFrame("service", fd);

                    svc.detectRegion("service");
                    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
                    auto rec = cache.read("service");

                    bool hasPlate = false;
                    std::string plateText;
                    for (const auto& it : rec.result.items) {
                        if (it.type == DetectionResult::Type::LICENSE_PLATE) {
                            hasPlate = it.detected;
                            plateText = it.value;
                            std::cout << "  [INFO] 10. license_plate detected=" << it.detected
                                      << " value='" << it.value << "' conf=" << it.confidence
                                      << std::endl;
                        }
                    }
                    CHECK(rec.seq > 0, "10. plate model round produced a result record");
                    CHECK(hasPlate, "10. 车牌被检出");
                    std::cout << "  [INFO] 10. 识别结果: '" << plateText << "'" << std::endl;
                }
                svc.stopDetection();
            } catch (const std::exception& e) {
                CHECK(false, std::string("10. plate model load/infer: ") + e.what());
            }
        }
    }

    std::cout << "========== " << g_pass << " passed, " << g_fail << " failed ==========" << std::endl;
    return g_fail == 0 ? 0 : 1;
}
