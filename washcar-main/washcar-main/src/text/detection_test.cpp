// detection_test.cpp — 检测服务自检。
// 覆盖两块:
//   1. AI 模型工厂 + 检测区域(模型去重/共享/卸载、区域绑模型、频率与去重、报警只报一次)
//   2. 检测服务(业务类别名解析、结果缓存、帧缓冲、ai 配置解析、构造生命周期、
//      构造失败抛异常、真实模型推理链路)
//
// 用法: ./detection_test
// 退出码: 0 = 全过, 非 0 = 失败用例数
//
// 本文件自包含: 断言助手在文件内, 不依赖其它测试文件。
#include "business_layer/detection/detection.h"
#include "business_layer/detection/detection_region.h"
#include "business_layer/detection/detection_result_cache.h"
#include "business_layer/device/real_time_frame_cache.h"
#include "common/ai/ai_model_factory.h"
#include "common/config/config_manager.h"
#include "common/log/log_manager.h"
#include "data_layer/camera/camera_utils.h"
#include "image_utils.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace washcar::cfg;
using washcar::ai::AiImage;
using washcar::ai::AiModelHit;
using washcar::ai::AiModelFactory;

namespace {

int g_pass = 0;
int g_fail = 0;

void expect(bool ok, const std::string& what) {
    if (ok) {
        ++g_pass;
        return;
    }
    ++g_fail;
    std::cout << "  [FAIL] " << what << std::endl;
}

void writeTempJson(const std::string& path, const std::string& body) {
    std::ofstream out(path);
    out << body;
}

// ---------- 区域用的假模型 ----------

// 假模型: 不碰 rknn, run() 返回预先摆好的命中项
class FakeModel : public washcar::ai::IAiModel {
public:
    static int liveCount;

    explicit FakeModel(std::string kind) : m_kind(std::move(kind)) { ++liveCount; }
    ~FakeModel() override { --liveCount; }

    bool load(const AiModelConfig&) override { return true; }
    void unload() override {}
    std::vector<AiModelHit> run(const AiImage&) override { return m_hits; }

    std::string m_kind;
    std::vector<AiModelHit> m_hits;
};
int FakeModel::liveCount = 0;

// 工厂按 kind 造模型; 测试这边留一份原始指针方便摆命中项
std::map<std::string, FakeModel*> g_models;

AiModelConfig modelConfig(const std::string& id, const std::string& kind,
                          std::vector<int> classIds, std::vector<std::string> classTypes) {
    AiModelConfig cfg;
    cfg.modelId = id;
    cfg.modelKind = kind;
    cfg.modelPaths = {"fake.rknn"};
    cfg.classIds = std::move(classIds);
    cfg.classTypes = std::move(classTypes);
    return cfg;
}

CameraRealTimeFrame makeRegionFrame() {
    FrameData data;
    data.width = 4;
    data.height = 4;
    data.timestamp_ = 1234;
    data.data = std::shared_ptr<uint8_t>(new uint8_t[4 * 4 * 3](), std::default_delete<uint8_t[]>());
    return CameraRealTimeFrame(data);
}

AiModelHit hit(int classId, const std::string& value = "") {
    AiModelHit h;
    h.classId = classId;
    h.detected = true;
    h.confidence = 0.9f;
    h.value = value;
    return h;
}

// ---------- 检测服务用的假帧 ----------

// 造一帧假数据(3x2 RGB), 只验证缓冲链路, 不跑推理
FrameData makeFrame() {
    FrameData fd;
    fd.width = 3;
    fd.height = 2;
    auto buf = new uint8_t[3 * 2 * 3];
    for (int i = 0; i < 18; ++i) buf[i] = static_cast<uint8_t>(i);
    fd.data = std::shared_ptr<uint8_t>(buf, [](uint8_t* p) { delete[] p; });
    fd.timestamp_ = 1000;
    return fd;
}

}  // namespace

int main(int argc, char** argv) {
    (void)argc;
    // 初始化最小日志(避免 LOG_* 拿到 null logger)
    auto queue = std::make_shared<BlockingQueue>();
    auto logger = std::make_shared<AsyncLogger>(queue);
    logger->addSink(std::make_shared<ConsoleSink>());
    logger->setLogLevel(LogLevel::WARNING);
    logger->start();
    LoggerManager::instance().registerLogger("work", logger);

    // =====================================================================
    // 1. AI 模型工厂 + 检测区域
    // =====================================================================
    ConfigManager::instance().applyDefaults();

    // --- 1.1 工厂: 同一 modelId 只加载一次 ---
    {
        AiModelFactory::instance().clear();
        AiModelFactory::instance().setCreatorForTest([](const std::string& kind) {
            auto model = std::make_unique<FakeModel>(kind);
            g_models[kind] = model.get();
            return model;
        });

        const AiModelConfig car = modelConfig("mCar", "car", {0}, {"CAR_PRESENT"});
        auto first = AiModelFactory::instance().acquire(car);
        auto second = AiModelFactory::instance().acquire(car);
        expect(first && second, "1.1 工厂返回非空");
        expect(first.get() == second.get(), "1.1 同一 modelId 拿到同一个实例");
        expect(FakeModel::liveCount == 1, "1.1 同一 modelId 只加载一次");

        const AiModelConfig person = modelConfig("mPerson", "person", {1}, {"PERSON_DETECTED"});
        auto other = AiModelFactory::instance().acquire(person);
        expect(other.get() != first.get(), "1.1 不同 modelId 是不同实例");
        expect(AiModelFactory::instance().loadedCount() == 2, "1.1 loadedCount=2");

        // 释放全部使用者 -> 过期 -> 再取要重新加载
        auto* carRaw = first.get();
        first.reset();
        second.reset();
        expect(FakeModel::liveCount == 1, "1.1 car 已释放, 只剩 person 在用");
        AiModelFactory::instance().releaseExpired();
        expect(AiModelFactory::instance().loadedCount() == 1, "1.1 只剩 person 还活着");
        auto again = AiModelFactory::instance().acquire(car);
        expect(again.get() != carRaw, "1.1 全部释放后重新加载是新实例");
        expect(FakeModel::liveCount == 2, "1.1 重新加载后 liveCount=2");

        // 两个区域共用同一个模型: 只多一个使用者, 不会多一份模型
        auto sharedByRegion = AiModelFactory::instance().acquire(car);
        expect(sharedByRegion.get() == again.get(), "1.1 第二个区域共用同一实例");
        expect(FakeModel::liveCount == 2, "1.1 共用不增加模型实例数");

        AiModelFactory::instance().clear();
        again.reset();
        sharedByRegion.reset();
        other.reset();
        AiModelFactory::instance().releaseExpired();
        expect(FakeModel::liveCount == 0, "1.1 全部释放后模型卸载");
        AiModelFactory::instance().setCreatorForTest(nullptr);
    }

    // --- 1.2 区域: 绑多模型 + 跨区域共享 + 同步跑一轮 ---
    {
        AiModelFactory::instance().clear();
        AiModelFactory::instance().setCreatorForTest([](const std::string& kind) {
            auto model = std::make_unique<FakeModel>(kind);
            g_models[kind] = model.get();
            return model;
        });

        const std::vector<AiModelConfig> models = {
            modelConfig("mCar", "car", {0}, {"CAR_PRESENT"}),
            modelConfig("mPerson", "person", {1}, {"PERSON_DETECTED"}),
        };

        AiRegionBinding queueBinding;
        queueBinding.regionId = "queue";
        queueBinding.idleModelIds = {"mCar", "mPerson"};
        queueBinding.workingModelIds = {"mCar", "mPerson"};
        DetectionRegion queue(queueBinding);
        std::string error;
        expect(queue.bindModels(models, error), "1.2 区域绑定模型成功: " + error);
        expect(queue.modelIds() == "mCar,mPerson", "1.2 模型 id 顺序与配置一致");
        expect(FakeModel::liveCount == 2, "1.2 两个模型各加载一次");

        AiRegionBinding serviceBinding;
        serviceBinding.regionId = "service";
        serviceBinding.idleModelIds = {"mCar"};
        serviceBinding.workingModelIds = {"mCar", "mPerson"};
        DetectionRegion service(serviceBinding);
        expect(service.bindModels(models, error), "1.2 第二个区域绑定成功");
        expect(FakeModel::liveCount == 2, "1.2 跨区域共用模型, 不重复加载");
        expect(service.modelIds() == "mCar", "1.2 低频模型子集生效");

        // 命中: person 模型报 PERSON_DETECTED
        g_models["person"]->m_hits = {hit(1)};
        auto frame = makeRegionFrame();
        auto items = queue.runModels(frame, frame.getTimestamp());
        bool personDetected = false;
        for (const auto& item : items) {
            if (item.type == DetectionResult::Type::PERSON_DETECTED) personDetected = item.detected;
        }
        expect(items.size() == 2, "1.2 每个类别的占位项都在");
        expect(personDetected, "1.2 模型命中映射到业务类型");

        // 未配置的模型 -> 绑定失败
        AiRegionBinding badBinding;
        badBinding.regionId = "bad";
        badBinding.idleModelIds = {"noSuchModel"};
        DetectionRegion bad(badBinding);
        expect(!bad.bindModels(models, error), "1.2 引用未定义模型时绑定失败");
        expect(error.find("noSuchModel") != std::string::npos, "1.2 错误信息带模型 id");
    }

    // --- 1.3 区域业务: 频率与去重 ---
    {
        AiRegionBinding binding;
        binding.regionId = "queue";
        binding.idleModelIds = {"mCar"};
        binding.workingModelIds = {"mCar"};
        DetectionRegion region(binding);

        expect(region.due(1000, 6000, 2000), "1.3 首次检测一定到期");
        region.markDetected(1000);
        expect(!region.due(1500, 6000, 2000), "1.3 低频 6s 内不到期");
        region.enterWorking();
        expect(region.due(3500, 6000, 2000), "1.3 高频 2s 后到期");

        std::vector<DetectionAlarm> alarms;
        std::vector<DetectionResult::Item> person = {
            {DetectionResult::Type::PERSON_DETECTED, true, "", 0.9f, 1, ""}};
        region.enterIdle();
        region.evaluate(person, 1, 3, alarms);
        expect(region.workingMode(), "1.3 低频检测到人 -> 升频");
        expect(alarms.size() == 1 && alarms[0].type == DetectionAlarm::Type::PERSON, "1.3 人报一次");

        alarms.clear();
        region.evaluate(person, 2, 3, alarms);
        expect(alarms.empty(), "1.3 人持续在场不重复报");

        // 高频下连续无人无车 -> 回低频
        const std::vector<DetectionResult::Item> emptyItems;
        for (int i = 0; i < 3; ++i) region.evaluate(emptyItems, 10 + i, 3, alarms);
        expect(!region.workingMode(), "1.3 连续无人无车回低频");

        // 异常: 状态变化才报
        region.enterWorking();
        alarms.clear();
        // 车门开着的时候车一定在(sim 里也要带上车, 否则区域会被判成"无人无车"回低频)
        std::vector<DetectionResult::Item> door = {
            {DetectionResult::Type::DOOR_OPEN, true, "", 0.8f, 20, ""},
            {DetectionResult::Type::CAR_PRESENT, true, "", 0.9f, 20, ""}};
        region.evaluate(door, 20, 3, alarms);
        expect(alarms.size() == 1 && alarms[0].itemType == DetectionResult::Type::DOOR_OPEN,
               "1.3 车门异常报一次");
        alarms.clear();
        region.evaluate(door, 21, 3, alarms);
        expect(alarms.empty(), "1.3 异常持续不重复报");
        alarms.clear();
        region.evaluate(emptyItems, 22, 3, alarms);
        region.evaluate(door, 23, 3, alarms);
        expect(alarms.size() == 1, "1.3 异常恢复后再出现重新报");
    }

    AiModelFactory::instance().clear();

    // =====================================================================
    // 2. 检测服务
    // =====================================================================

    // --- 2.1 业务规定: 检测结果必须带图像数据 ---
    // 这条既守住"字段不能被删"(删了这个文件就编不过),也守住
    // "写进结果缓冲后图像数据还在、而且是共享视图不是深拷贝"。
    {
        DetectionResultCache imageCache;
        std::shared_ptr<uint8_t> pixels(new uint8_t[640 * 480 * 3]());
        DetectionRecord imageRecord;
        imageRecord.seq = 1;
        imageRecord.cameraId = "service";
        imageRecord.imageData.data = pixels;
        imageRecord.imageData.width = 640;
        imageRecord.imageData.height = 480;
        imageCache.write("service", imageRecord);

        const DetectionRecord readBack = imageCache.read("service");
        expect(readBack.imageData.valid(), "2.1 image data survives the result cache");
        expect(readBack.imageData.size() == 640u * 480u * 3u, "2.1 image data size matches the frame");
        expect(readBack.imageData.data.get() == pixels.get(), "2.1 image data is a shared view, not a copy");
    }

    // --- 2.2 初始化数据目录(不初始化的话 ai.db 打不开, 检测结果不会落库) ---
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

    // --- 2.3 业务类别名 <-> 枚举 ---
    {
        DetectionResult::Type t;
        expect(typeFromString("CAR_PRESENT", t) && t == DetectionResult::Type::CAR_PRESENT,
               "2.3 typeFromString('CAR_PRESENT') ok");
        expect(typeFromString("PERSON_DETECTED", t) && t == DetectionResult::Type::PERSON_DETECTED,
               "2.3 typeFromString('PERSON_DETECTED') ok");
        expect(!typeFromString("UNKNOWN", t), "2.3 typeFromString('UNKNOWN') returns false");
        expect(typeToString(DetectionResult::Type::DOOR_OPEN) == "DOOR_OPEN",
               "2.3 typeToString(DOOR_OPEN) round-trip");
    }

    // --- 2.4 DetectionResultCache 写入/读取/同源覆盖/clear ---
    {
        DetectionResultCache cache;
        DetectionRecord r1;
        r1.seq = 1; r1.cameraId = "service"; r1.callTime = 10; r1.inferTime = 20;
        DetectionRecord r2 = r1;
        r2.seq = 2; r2.callTime = 30; r2.inferTime = 40;

        expect(cache.read("service").seq == 0, "2.4 read before write -> seq==0 (无效)");

        cache.write("service", r1);
        expect(cache.read("service").seq == 1, "2.4 write -> read seq=1");

        cache.write("service", r2);   // 同源新结果覆盖旧结果
        expect(cache.read("service").seq == 2, "2.4 same-source new result overwrites old");

        expect(cache.read("queue").seq == 0, "2.4 other region unaffected");

        cache.clear();
        expect(cache.read("service").seq == 0, "2.4 clear -> seq==0");
    }

    // --- 2.5 ConfigManager 解析 ai 配置(模型定义/区域-模型对/频率) ---
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
        expect(ok, "2.5 ai config JSON loads");
        const auto& ai = ConfigManager::instance().ai();
        expect(ai.idleIntervalMs == 5000 && ai.workingIntervalMs == 1000
                   && ai.exitWorkingEmptyTimes == 10, "2.5 frequencies parsed from JSON");
        expect(ai.modelConfigs.size() == 2 && ai.modelConfigs[0].modelId == "vehicle"
                   && ai.modelConfigs[0].classIds[0] == 0
                   && ai.modelConfigs[0].classTypes[0] == "CAR_PRESENT",
               "2.5 modelConfigs parsed from JSON");
        expect(ai.regions.size() == 1 && ai.regions[0].regionId == "service"
                   && ai.regions[0].idleModelIds.size() == 2, "2.5 regions parsed from JSON");
        std::remove(path.c_str());
    }

    // --- 2.6 不接 AI(aiEnabled=false) 构造 + 起停生命周期 ---
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
            expect(true, "2.6 empty modelConfigs (AI off) construct + start/stop ok");
        } catch (const std::exception& e) {
            expect(false, std::string("2.6 empty modelConfigs should NOT throw: ") + e.what());
        }
    }

    // --- 2.7 区域引用未定义模型 -> 构造抛异常 ---
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
        expect(threw, "2.7 region referencing undefined model -> constructor throws");
    }

    // --- 2.8 modelPaths 空 -> 模型被跳过, 不拖垮主程序 ---
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
        expect(!threw, "2.8 empty modelPaths -> 该模型跳过, 构造不抛异常");
    }

    // --- 2.9 帧缓冲(RealTimeFrameCache)写入/读取/同源覆盖 ---
    {
        RealTimeFrameCache frameCache;
        frameCache.updateCameraRealTimeFrame("service", makeFrame());
        frameCache.updateCameraRealTimeFrame("service", makeFrame());   // 同源覆盖
        CameraRealTimeFrame f = frameCache.getCameraRealTimeFrame(CameraInfo("service"));
        expect(f.isValid(), "2.9 frame cache write+read valid");
        CameraRealTimeFrame g = frameCache.getCameraRealTimeFrame(CameraInfo("queue"));
        expect(!g.isValid(), "2.9 un-written camera -> invalid frame");
    }

    // --- 2.10 检测服务(无模型) 新接口调用不崩 ---
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
        expect(true, "2.10 detectRegion/detectAllRegions/frequency calls don't crash");
    }

    // --- 2.11 真实 yolov8 模型(模型/测试图存在时; 验证加载+推理+结果链路) ---
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
            std::cout << "  [SKIP] 2.11 真实模型/测试图不存在, 跳过 (model=" << modelPath
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
                    expect(false, "2.11 read_image failed");
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
                            std::cout << "  [INFO] 2.11 person detected, conf=" << it.confidence << std::endl;
                        }
                    }
                    expect(rec.seq > 0, "2.11 real model round produced a result record");
                    expect(hasPerson, "2.11 bus.jpg 检出 person (COCO class 0)");
                    svc.stopDetection();
                }
            } catch (const std::exception& e) {
                expect(false, std::string("2.11 real model load/infer: ") + e.what());
            }
        }
    }

    // --- 2.12 车牌模型(检测+识别两级串联, 用现场帧验证) ---
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
            std::cout << "  [SKIP] 2.12 车牌模型/测试图不存在, 跳过" << std::endl;
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
                    expect(false, "2.12 read_image failed");
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
                            std::cout << "  [INFO] 2.12 license_plate detected=" << it.detected
                                      << " value='" << it.value << "' conf=" << it.confidence
                                      << std::endl;
                        }
                    }
                    expect(rec.seq > 0, "2.12 plate model round produced a result record");
                    expect(hasPlate, "2.12 车牌被检出");
                    std::cout << "  [INFO] 2.12 识别结果: '" << plateText << "'" << std::endl;
                }
                svc.stopDetection();
            } catch (const std::exception& e) {
                expect(false, std::string("2.12 plate model load/infer: ") + e.what());
            }
        }
    }

    std::cout << "detection_test: pass=" << g_pass << ", fail=" << g_fail << std::endl;
    return g_fail == 0 ? 0 : 1;
}
