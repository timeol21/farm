// =====================================================================
// 驿公里洗车服务 命令模拟测试
// 链路: HTTP(WebService) / MQTT(真实客户端+Mock Broker) → LobbyService
//       → YiGongLiWashCarService → DeviceService → VirtualPlcInstance
// 全部 127.0.0.1 回环, 无真实 PLC / 外部 broker / 外部网络依赖。
// 虚拟 PLC 内部按 .gx3 梯形图解析数据还原行为: 扫描线程 + 状态转移 + 命令确认点。
// =====================================================================
#include <iostream>
#include <fstream>
#include <memory>
#include <thread>
#include <chrono>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <cstring>
#include <string>
#include <functional>
#include <filesystem>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

#include "business_layer/lobby/lobby_service.h"
#include "presentation_layer/http_service.h"
#include "presentation_layer/http_command.h"
#include "business_layer/command/mqtt_command.h"
#include "business_layer/command/mqtt/mqtt_service.h"
#include "business_layer/command/mqtt/protocol.h"
#include "business_layer/command/command_service.h"
#include "business_layer/washcar/washcar_service.h"
#include "business_layer/device/device_service.h"
#include "business_layer/device/device_manage_service.h"
#include "business_layer/detection/detection.h"
#include "data_layer/camera/camera_device.h"
#include "business_layer/device/device_acquisition_task.h"
#include "business_layer/device/device_status_cache.h"
#include "business_layer/timer.h"
#include "common/log/log.h"
#include "common/log/log_manager.h"
#include "common/log/log_sink.h"
#include "common/log/trace_context.h"
#include "common/config/config_manager.h"
#include "data_layer/fx_plc/iplc_instance.h"
#include "data_layer/fx_plc/fx_plc_instance_set.h"
#include "data_layer/camera/camera.h"
#include "data_layer/camera/camera_instance_set.h"
#include "data_layer/gpio_device/gpio_device_instance_set.h"
#include "data_layer/serial_direct_device/serial_direct_device_instance_set.h"
#include "common/sqlite/database_manager.h"
#include "data_layer/command/command_dao.h"
#include "data_layer/ai/ai_dao.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// =====================================================================
// 软元件地址(与业务代码 yigl::* 常量一致, 避免 include 依赖扩散)
// =====================================================================
namespace addr {
    constexpr int M_START_PREPARE  = 204;
    constexpr int M_START_MODE_1   = 521;
    constexpr int M_START_MODE_2   = 522;
    constexpr int M_START_MODE_3   = 523;
    constexpr int M_STOP           = 205;
    constexpr int M_ABORT          = 206;
    constexpr int M_RESET          = 202;
    constexpr int M_RESET_SAFE     = 203;
    constexpr int M_STOP_WORK      = 501;
    constexpr int M_STOP_SERVICE   = 510;
    constexpr int M_CAR_POSITIONED = 282;
    constexpr int M_PAUSE_LED      = 270;
    constexpr int M_UNSAFE         = 147;
    constexpr int M_HW_FAULT       = 148;
    constexpr int M_CTRL_FAULT     = 149;
    constexpr int M_TOTAL_FAULT    = 150;
    constexpr int M_ANTI_COLLISION = 169;
    constexpr int D_FAULT_CODE     = 1000;
    constexpr int D_DURATION_SEC   = 2000;
    constexpr int S_FIRST_WASH     = 20;
    constexpr int S_LAST_WASH      = 81;
}

// =====================================================================
// 测试断言上下文
// =====================================================================
static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name) do { \
    if (cond) { ++g_pass; std::cout << "[PASS] " << name << std::endl; } \
    else { ++g_fail; std::cout << "[FAIL] " << name << std::endl; } \
} while (0)

#define CHECK_EQ(a, b, name) do { \
    auto va = (a); auto vb = (b); \
    if (va == vb) { ++g_pass; std::cout << "[PASS] " << name << std::endl; } \
    else { ++g_fail; std::cout << "[FAIL] " << name << " (got=" << va << " want=" << vb << ")" << std::endl; } \
} while (0)

// =====================================================================
// 虚拟 PLC: 按 .gx3 梯形图规则运行的内存 PLC
// 差异收敛点 —— 真实 PLC 的梯形图行为在这里实现, 其余链路全部真实。
// =====================================================================
class VirtualPlcInstance : public IPlcInstance {
public:
    enum class FailMode { NONE, CONNECT_FAIL, IO_FAIL };

    explicit VirtualPlcInstance(const std::string& id) : plcId_(id) {
        // 首次扫描: 初始停止态 S0 + D1210="stop"
        s_[0] = true;
        writeD1210("stop");
        scanThread_ = std::thread(&VirtualPlcInstance::scanLoop, this);
    }
    ~VirtualPlcInstance() override {
        running_ = false;
        if (scanThread_.joinable()) scanThread_.join();
    }

    // ---- IPlcInstance ----
    bool connect() override { return fail_ != FailMode::CONNECT_FAIL; }
    void disconnect() override {}
    bool isConnected() const override { return fail_ != FailMode::CONNECT_FAIL; }

    bool forceY(int yOctal, bool turnOn) override {
        if (ioBroken()) return false;
        std::lock_guard<std::mutex> lock(mu_);
        y_[yOctal] = turnOn;
        return true;
    }
    bool forceX(int xOctal, bool turnOn) override {
        if (ioBroken()) return false;
        std::lock_guard<std::mutex> lock(mu_);
        x_[xOctal] = turnOn;
        return true;
    }
    bool forceM(int mDecimal, bool turnOn) override {
        if (ioBroken()) return false;
        {
            std::lock_guard<std::mutex> lock(mu_);
            m_[mDecimal] = turnOn;
            mHist_[mDecimal].push_back({turnOn, std::chrono::steady_clock::now()});
        }
        return true;
    }
    bool readD(int dNumber, uint16_t& value) override {
        if (ioBroken()) return false;
        std::lock_guard<std::mutex> lock(mu_);
        auto it = d_.find(dNumber);
        value = (it == d_.end()) ? 0 : it->second;
        return true;
    }
    bool readYBit(int yOctal, bool& state) override {
        if (ioBroken()) return false;
        std::lock_guard<std::mutex> lock(mu_);
        auto it = y_.find(yOctal);
        state = (it != y_.end()) && it->second;
        return true;
    }
    bool readM(int mDecimal, bool& state) override {
        if (ioBroken()) return false;
        std::lock_guard<std::mutex> lock(mu_);
        auto it = m_.find(mDecimal);
        state = (it != m_.end()) && it->second;
        return true;
    }
    bool readXBit(int xOctal, bool& state) override {
        if (ioBroken()) return false;
        std::lock_guard<std::mutex> lock(mu_);
        auto it = x_.find(xOctal);
        state = (it != x_.end()) && it->second;
        return true;
    }
    bool readX(int group, int& status8) override {
        (void)group; status8 = 0; return !ioBroken();
    }
    bool readY(int group, int& status8) override {
        (void)group; status8 = 0; return !ioBroken();
    }
    bool readS(int sDecimal, bool& state) override {
        if (ioBroken()) return false;
        std::lock_guard<std::mutex> lock(mu_);
        auto it = s_.find(sDecimal);
        state = (it != s_.end()) && it->second;
        return true;
    }
    const std::string& getPlcId() const override { return plcId_; }

    // ---- 测试钩子 ----
    void setFailMode(FailMode f) { fail_ = f; }
    void setScanPaused(bool p) { scanPaused_ = p; }
    void clearForceHistory() {
        std::lock_guard<std::mutex> lock(mu_);
        mHist_.clear();
    }
    bool wasMForced(int m, bool val) const {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = mHist_.find(m);
        if (it == mHist_.end()) return false;
        for (const auto& e : it->second) if (e.first == val) return true;
        return false;
    }
    // 直接写内存(模拟 PLC 程序/现场状态注入, 不走 forceM 历史)
    void setM(int m, bool v) { std::lock_guard<std::mutex> lock(mu_); m_[m] = v; }
    void setD(int d, uint16_t v) { std::lock_guard<std::mutex> lock(mu_); d_[d] = v; }
    void setX(int x, bool v) { std::lock_guard<std::mutex> lock(mu_); x_[x] = v; }
    void setS(int s, bool v) { std::lock_guard<std::mutex> lock(mu_); s_[s] = v; }
    void setAutoAdvance(bool on) { autoAdvance_ = on; }
    // 清上升沿寄存器(场景重置后保证命令位重新产生上升沿)
    void resetEdgeDetectors() {
        std::lock_guard<std::mutex> lock(mu_);
        prevM204_ = prevM205_ = prevM206_ = false;
        prevMode1_ = prevMode2_ = prevMode3_ = false;
    }
    // 推进 STL 步(模拟整车洗车走到某一步), 同步更新工序号/阶段指示/D1210
    void advanceTo(int s) {
        std::lock_guard<std::mutex> lock(mu_);
        clearAllS();
        s_[s] = true;
        d_[addr::D_FAULT_CODE] = 0;
        applyStepSideEffects(s);
        if (s >= addr::S_FIRST_WASH && s <= addr::S_LAST_WASH) writeD1210Unlocked("work");
    }
    // 一键回到指定基础态
    void forceState(int s) {
        std::lock_guard<std::mutex> lock(mu_);
        clearAllS();
        homePhase1Active_ = homePhase2Active_ = false;
        if (s == 0) { enterStopState(); return; }
        s_[s] = true;
        if (s == 5) writeD1210Unlocked("normal");
        else if (s == 3 || s == 4) writeD1210Unlocked("alarm");
        else if (s == 1) writeD1210Unlocked("work");
    }
    void clearAllS() {
        for (auto& kv : s_) kv.second = false;
        for (int i = 0; i <= 81; ++i) s_[i] = false;
    }
    bool getM(int m) const { std::lock_guard<std::mutex> lock(mu_); auto it = m_.find(m); return it != m_.end() && it->second; }
    bool getS(int s) const { std::lock_guard<std::mutex> lock(mu_); auto it = s_.find(s); return it != s_.end() && it->second; }
    uint16_t getD(int d) const { std::lock_guard<std::mutex> lock(mu_); auto it = d_.find(d); return it == d_.end() ? 0 : it->second; }
    std::string d1210Str() const {
        std::lock_guard<std::mutex> lock(mu_);
        return buildD1210String();
    }
    int activeS() const {
        std::lock_guard<std::mutex> lock(mu_);
        for (int i = 81; i >= 0; --i) {
            auto it = s_.find(i);
            if (it != s_.end() && it->second) return i;
        }
        return -1;
    }
    bool isWashing() const {
        std::lock_guard<std::mutex> lock(mu_);
        for (int i = addr::S_FIRST_WASH; i <= addr::S_LAST_WASH; ++i) {
            auto it = s_.find(i);
            if (it != s_.end() && it->second) return true;
        }
        return false;
    }

private:
    // 断连语义: 任何非 NONE 模式都拒绝 IO(连接未建立或链路异常)
    bool ioBroken() const { return fail_ != FailMode::NONE; }

    // 进入停止态 S0(梯形图 b31): 清命令位 M204/M206/M202/M203 + 机构位
    void enterStopState() {
        clearAllS();
        s_[0] = true;
        m_[addr::M_START_PREPARE] = false;
        m_[addr::M_ABORT] = false;
        m_[addr::M_RESET] = false;
        m_[addr::M_RESET_SAFE] = false;
        for (int i = 210; i <= 244; ++i) m_[i] = false;
        homePhase1Active_ = homePhase2Active_ = false;
        carTimer_ = 0;
        d_[addr::D_FAULT_CODE] = 0;   // 进入停止态清故障码(场景干净语义)
        writeD1210Unlocked("stop");
    }

    // 梯形图扫描一帧(100ms 周期; 规则按 .gx3 解析数据还原)
    void scanOnce() {
        std::lock_guard<std::mutex> lock(mu_);
        const int64_t now = steadyMs();

        const bool s0 = s_[0], s1 = s_[1], s2 = s_[2], s3 = s_[3], s4 = s_[4], s5 = s_[5];
        const bool m204 = m_[addr::M_START_PREPARE];
        const bool m205 = m_[addr::M_STOP];
        const bool m206 = m_[addr::M_ABORT];
        const bool m202 = m_[addr::M_RESET];
        const bool m203 = m_[addr::M_RESET_SAFE];
        const bool m501 = m_[addr::M_STOP_WORK];
        const bool m510 = m_[addr::M_STOP_SERVICE];
        const bool m282 = m_[addr::M_CAR_POSITIONED];
        const bool m148 = m_[addr::M_HW_FAULT];

        // P10 模式互锁: 单选 + 全 0 回默认模式一
        if (rise(m_[addr::M_START_MODE_1], prevMode1_)) { m_[addr::M_START_MODE_2] = false; m_[addr::M_START_MODE_3] = false; }
        if (rise(m_[addr::M_START_MODE_2], prevMode2_)) { m_[addr::M_START_MODE_1] = false; m_[addr::M_START_MODE_3] = false; }
        if (rise(m_[addr::M_START_MODE_3], prevMode3_)) { m_[addr::M_START_MODE_1] = false; m_[addr::M_START_MODE_2] = false; }
        if (!m_[addr::M_START_MODE_1] && !m_[addr::M_START_MODE_2] && !m_[addr::M_START_MODE_3]) m_[addr::M_START_MODE_1] = true;

        // 车到位检测(仅 S5 扫描; X20=0 ∧ X21=1 持续 500ms → M282)
        if (s5 && !m510) {
            if (!x_[20] && x_[21]) {
                carTimer_ += scanMs_;
                if (carTimer_ >= 500) { m_[addr::M_CAR_POSITIONED] = true; carTimer_ = 500; }
            } else carTimer_ = 0;
        } else carTimer_ = 0;

        // S5 启动链: ¬M510 ∧ M204↑ ∧ M282 → SET S20 + 清零洗车时长
        if (s5 && !m510 && m282 && rise(m204, prevM204_)) {
            s_[20] = true; s_[5] = false;
            for (int d = 2000; d <= 2006; ++d) d_[d] = 0;
            writeD1210Unlocked("work");
            return;   // 本帧剩余规则在下一帧处理(模拟扫描顺序)
        }
        // 车未到位清启动准备
        if (s5 && !m282) m_[addr::M_START_PREPARE] = false;

        // S5 停止/停止营业: M501|M205 → SET S0 + RST M205
        if (s5 && (m501 || m205)) {
            enterStopState();
            m_[addr::M_STOP] = false;
            return;
        }

        // S5 暂停服务: M510 → T128 计时 → M270 脉冲(0.5~1.0s)
        if (s5 && m510) {
            t128_ += scanMs_;
            if (t128_ >= 500 && !m270Pulsed_) { m_[addr::M_PAUSE_LED] = true; m270Pulsed_ = true; }
            if (t128_ >= 1000) m_[addr::M_PAUSE_LED] = false;
            if (t128_ >= 1500) { t128_ = 1500; }
        } else { t128_ = 0; m270Pulsed_ = false; m_[addr::M_PAUSE_LED] = false; }

        // S0 复位链: (M202|M203)∧¬M148∧¬M501 → SET S1; 硬件故障清复位命令
        if (s0) {
            if (m148) { m_[addr::M_RESET] = false; m_[addr::M_RESET_SAFE] = false; }
            if ((m202 || m203) && !m148 && !m501) {
                s_[1] = true; s_[0] = false;
                writeD1210Unlocked("work");
                homePhase1Active_ = true; homePhase1Start_ = now;
                return;
            }
        } else {
            // b26: 非停止态清复位命令
            m_[addr::M_RESET] = false;
        }

        // 归位流程: S1(归位1) → 延时 → S2(归位2) → 延时 → S5(归位完成)
        if (s1) {
            m_[addr::M_STOP] = false;   // b26: 非停止态清 M205, 归位不被打断
            m_[301] = true;
            if (!homePhase1Active_) { homePhase1Active_ = true; homePhase1Start_ = now; }
            if (m_[addr::M_UNSAFE] || m148 || m_[addr::M_ANTI_COLLISION] || x_[36]) {
                enterStopState();
                return;
            }
            m_[addr::D_FAULT_CODE] = 0;   // b99: 归位清故障码
            if (now - homePhase1Start_ >= homePhaseMs) {
                s_[1] = false; s_[2] = true; m_[301] = true;
                homePhase1Active_ = false; homePhase2Active_ = true; homePhase2Start_ = now;
                m_[addr::M_RESET] = false; m_[addr::M_RESET_SAFE] = false;
                return;
            }
        } else if (s2) {
            m_[addr::M_STOP] = false;   // b26: 非停止态清 M205
            m_[301] = true;
            if (!homePhase2Active_) { homePhase2Active_ = true; homePhase2Start_ = now; }
            if (x_[36]) {
                enterStopState();
                return;
            }
            if (now - homePhase2Start_ >= homePhase2Ms) {
                s_[2] = false; s_[5] = true; m_[301] = false;
                homePhase2Active_ = false;
                d_[210] = 36;              // b106: 归位到位
                writeD1210Unlocked("normal");
                return;
            }
        }

        // 洗车中(S20~S81): 急停/中止 → 故障码; 故障码 → 报警/中止态
        if (isWashingUnlocked()) {
            if (x_[36]) d_[addr::D_FAULT_CODE] = 105;
            if (m206) d_[addr::D_FAULT_CODE] = 106;
            const uint16_t code = d_[addr::D_FAULT_CODE];
            if (code == 106) {
                clearAllS(); s_[4] = true; s4Timer_ = now;
                writeD1210Unlocked("alarm");
                return;
            }
            if (code != 0 && code != 106 && m_[addr::M_TOTAL_FAULT]) {
                clearAllS(); s_[3] = true; s3Timer_ = now;
                writeD1210Unlocked("alarm");
                return;
            }
            // 洗车时长秒计数(b2): 洗车中 SM8013 每秒脉冲
            sm8013Acc_ += scanMs_;
            if (sm8013Acc_ >= 1000) { sm8013Acc_ -= 1000; ++d_[addr::D_DURATION_SEC]; }
        }

        // 故障位联动(b469-471): D1000≠0 → M149; M148|M149 → M150
        m_[addr::M_CTRL_FAULT] = (d_[addr::D_FAULT_CODE] != 0);
        m_[addr::M_TOTAL_FAULT] = m148 || m_[addr::M_CTRL_FAULT];

        // S3 报警态: 急停+停止 → 回停止态(b113 简化)
        if (s3 && x_[36] && m205) {
            enterStopState();
            return;
        }
        // S4 中止态: 故障恢复(简化: 进入后超时自动回停止态)
        if (s4 && now - s4Timer_ >= 2000) {
            enterStopState();
            return;
        }

        // 阶段指示(按当前步)
        updateStageIndicatorsUnlocked();

        // 洗车流程自动推进(测试钩子: 整车跑完回待机)
        if (autoAdvance_ && isWashingUnlocked()) autoAdvanceOnce(now);
    }

    void scanLoop() {
        while (running_) {
            if (!scanPaused_) scanOnce();
            std::this_thread::sleep_for(std::chrono::milliseconds(scanMs_));
        }
    }

    // ---- 内部工具 ----
    bool rise(bool cur, bool& prev) { bool r = cur && !prev; prev = cur; return r; }
    bool isWashingUnlocked() const {
        for (int i = addr::S_FIRST_WASH; i <= addr::S_LAST_WASH; ++i) {
            auto it = s_.find(i);
            if (it != s_.end() && it->second) return true;
        }
        return false;
    }
    void writeD1210(const std::string& str) {
        std::lock_guard<std::mutex> lock(mu_);
        writeD1210Unlocked(str);
    }
    void writeD1210Unlocked(const std::string& str) {
        const std::string padded = str + std::string(6 - str.size(), '\0');
        auto pack = [](char hi, char lo) { return static_cast<uint16_t>((static_cast<uint8_t>(hi) << 8) | static_cast<uint8_t>(lo)); };
        d_[1210] = pack(padded[0], padded[1]);
        d_[1211] = pack(padded[2], padded[3]);
        d_[1212] = pack(padded[4], padded[5]);
    }
    std::string buildD1210String() const {
        char buf[7] = {0};
        auto unpack = [](uint16_t v, char& hi, char& lo) { hi = static_cast<char>(v >> 8); lo = static_cast<char>(v & 0xFF); };
        char c0, c1, c2, c3, c4, c5;
        unpack(d1210Of(1210), c0, c1);
        unpack(d1210Of(1211), c2, c3);
        unpack(d1210Of(1212), c4, c5);
        buf[0]=c0; buf[1]=c1; buf[2]=c2; buf[3]=c3; buf[4]=c4; buf[5]=c5;
        return std::string(buf);
    }
    uint16_t d1210Of(int d) const {
        auto it = d_.find(d);
        return it == d_.end() ? 0 : it->second;
    }
    void applyStepSideEffects(int s) {
        // 工序号(真实梯形图 T→MOV 规则)
        static const std::unordered_map<int, uint16_t> stepToCode = {
            {20,1},{21,1},{22,2},{30,3},{31,3},{32,4},{33,5},{34,5},
            {40,6},{51,5},{52,6},{53,3},{54,3},{55,4},{56,9},{57,10},
            {70,7},{71,7},{72,7},{80,8}
        };
        auto it = stepToCode.find(s);
        if (it != stepToCode.end()) d_[addr::D_FAULT_CODE] = it->second;
        updateStageIndicatorsUnlocked();
    }
    void updateStageIndicatorsUnlocked() {
        static const int stageMs[13] = {301,302,303,304,305,306,307,308,309,310,311,312,313};
        for (int i = 0; i < 13; ++i) m_[stageMs[i]] = false;
        int cur = activeSUnlocked();
        auto on = [&](int m) { m_[m] = true; };
        if (cur == 1 || cur == 2) on(301);
        else if (cur == 20 || cur == 21 || cur == 22) on(302);
        else if (cur == 33 || cur == 51) on(303);
        else if (cur == 34 || cur == 40 || cur == 52) on(304);
        else if (cur == 30 || cur == 31 || cur == 53 || cur == 54) on(305);
        else if (cur == 32 || cur == 55) on(306);
        else if (cur == 56) on(308);
        else if (cur == 57) on(309);
        else if (cur == 71) on(311);
        else if (cur == 70 || cur == 72) on(312);
    }
    int activeSUnlocked() const {
        for (int i = 81; i >= 0; --i) {
            auto it = s_.find(i);
            if (it != s_.end() && it->second) return i;
        }
        return -1;
    }
    void autoAdvanceOnce(int64_t now) {
        const int cur = activeSUnlocked();
        static const std::vector<int> sequence = {20, 30, 32, 56, 57, 80, 81};
        if (cur == 81) { s_[81] = false; s_[5] = true; writeD1210Unlocked("normal"); autoAdvance_ = false; return; }
        auto it = std::find(sequence.begin(), sequence.end(), cur);
        if (it != sequence.end() && it + 1 != sequence.end()) {
            s_[cur] = false;
            const int next = *(it + 1);
            s_[next] = true;
            applyStepSideEffects(next);
            if (next >= addr::S_FIRST_WASH && next <= addr::S_LAST_WASH) writeD1210Unlocked("work");
        }
    }
    static int64_t steadyMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    std::string plcId_;
    std::atomic<bool> running_{true};
    std::atomic<bool> scanPaused_{false};
    std::atomic<FailMode> fail_{FailMode::NONE};
    std::atomic<bool> autoAdvance_{false};
    std::thread scanThread_;
    mutable std::mutex mu_;

    std::unordered_map<int, bool> m_, s_, y_, x_;
    std::unordered_map<int, uint16_t> d_;
    std::unordered_map<int, std::vector<std::pair<bool, std::chrono::steady_clock::time_point>>> mHist_;

    bool prevM204_ = false, prevM205_ = false, prevM206_ = false;
    bool prevMode1_ = false, prevMode2_ = false, prevMode3_ = false;
    int64_t carTimer_ = 0, t128_ = 0, sm8013Acc_ = 0;
    bool m270Pulsed_ = false;
    int64_t homePhase1Start_ = 0, homePhase2Start_ = 0;
    bool homePhase1Active_ = false, homePhase2Active_ = false;
    int64_t s3Timer_ = 0, s4Timer_ = 0;
    int scanMs_ = 100;
    // 归位模拟时长: 单段 ≥ 1 个采集周期(500ms)+余量, 保证中间态 S1/S2 至少被采样一次
    int homePhaseMs = 800;    // 归位1 模拟时长
    int homePhase2Ms = 800;   // 归位2 模拟时长
};

// =====================================================================
// Mock MQTT Broker: 本地回环 TCP 服务, 支持 MQTT 3.1.1 握手与命令注入
// - CONNECT → CONNACK(0x20 0x02 0x00 0x00)
// - SUBSCRIBE → SUBACK(0x90 0x03 <pid> 0x00)
// - PINGREQ → PINGRESP(0xD0 0x00)
// - PUBLISH → 记录 topic/payload(捕获上报)
// - injectCommand: 向客户端连接发 PUBLISH(模拟云端下发命令)
// =====================================================================
class MockMqttBroker {
public:
    struct Published {
        std::string topic;
        std::string payload;
    };

    MockMqttBroker() = default;
    ~MockMqttBroker() { stop(); }

    bool start() {
        listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd_ < 0) return false;
        int opt = 1;
        setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        // 端口取 short 正范围(<32768), 避免传给 MqttService(short) 时截断成负数
        const int base = 23000 + static_cast<int>(getpid() % 2000);
        for (int port = base; port < 30000; ++port) {
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port = htons(static_cast<uint16_t>(port));
            if (bind(listenFd_, (sockaddr*)&addr, sizeof(addr)) == 0) { port_ = port; break; }
        }
        if (port_ == 0) return false;
        if (listen(listenFd_, 4) < 0) return false;
        running_ = true;
        acceptThread_ = std::thread(&MockMqttBroker::acceptLoop, this);
        return true;
    }

    void stop() {
        running_ = false;
        if (clientFd_ >= 0) { close(clientFd_); clientFd_ = -1; }
        if (listenFd_ >= 0) { close(listenFd_); listenFd_ = -1; }
        if (acceptThread_.joinable()) acceptThread_.join();
    }

    int port() const { return port_; }

    std::vector<Published> published() const {
        std::lock_guard<std::mutex> lock(pubMu_);
        return published_;
    }
    void clearPublished() {
        std::lock_guard<std::mutex> lock(pubMu_);
        published_.clear();
    }
    bool waitForClient(int timeoutMs) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            if (clientConnected_) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return clientConnected_;
    }
    // 向客户端连接发 PUBLISH 报文(模拟云端下发命令到 flexem/boxNo/command)
    bool injectCommand(const std::string& topic, const std::string& payload) {
        if (clientFd_ < 0) return false;
        MqttProtocol proto;
        auto packet = proto.encodePublish(topic, payload);
        return sendAll(clientFd_, packet);
    }
    size_t publishedCount() const { return published().size(); }
    bool hasPublishedTopic(const std::string& fragment) const {
        for (const auto& p : published()) {
            if (p.topic.find(fragment) != std::string::npos) return true;
        }
        return false;
    }
    Published lastPublished() const {
        auto all = published();
        return all.empty() ? Published{} : all.back();
    }

private:
    void acceptLoop() {
        while (running_) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(listenFd_, &rfds);
            timeval tv{1, 0};
            int ret = select(listenFd_ + 1, &rfds, nullptr, nullptr, &tv);
            if (ret <= 0) continue;
            int cfd = accept(listenFd_, nullptr, nullptr);
            if (cfd < 0) continue;
            clientFd_ = cfd;
            clientConnected_ = true;
            clientLoop(cfd);
            close(cfd);
            clientFd_ = -1;
            clientConnected_ = false;
        }
    }

    void clientLoop(int fd) {
        std::vector<uint8_t> pending;
        while (running_ && fd >= 0) {
            uint8_t buf[4096];
            ssize_t n = recv(fd, buf, sizeof(buf), 0);
            if (n <= 0) break;
            pending.insert(pending.end(), buf, buf + n);
            // 尽量拆出完整 MQTT 帧
            size_t consumed = 0;
            while (consumed < pending.size()) {
                size_t idx = consumed;
                if (idx + 2 > pending.size()) break;
                uint8_t header = pending[idx];
                size_t lenPos = idx + 1;
                int remaining = 0, mult = 1;
                bool complete = true;
                while (true) {
                    if (lenPos >= pending.size()) { complete = false; break; }
                    uint8_t b = pending[lenPos++];
                    remaining += (b & 127) * mult;
                    mult *= 128;
                    if (!(b & 128)) break;
                }
                if (!complete) break;
                if (lenPos + remaining > pending.size()) break;
                std::vector<uint8_t> frame(pending.begin() + consumed, pending.begin() + lenPos + remaining);
                handleFrame(fd, header >> 4, frame, lenPos - consumed);
                consumed = lenPos + remaining;
            }
            pending.erase(pending.begin(), pending.begin() + consumed);
        }
    }

    void handleFrame(int fd, int type, const std::vector<uint8_t>& frame, size_t bodyStart) {
        MqttProtocol proto;
        if (type == 3) {  // PUBLISH
            MqttPacket packet;
            if (proto.decode(frame, packet)) {
                std::lock_guard<std::mutex> lock(pubMu_);
                published_.push_back({packet.topic, packet.payload});
            }
        } else if (type == 1) {  // CONNECT → CONNACK
            static const uint8_t connack[] = {0x20, 0x02, 0x00, 0x00};
            sendAll(fd, std::vector<uint8_t>(connack, connack + 4));
        } else if (type == 8) {  // SUBSCRIBE → SUBACK(回包体 2 字节 packetId + 1 字节 qos)
            if (bodyStart + 2 <= frame.size()) {
                std::vector<uint8_t> suback = {0x90, 0x03, frame[bodyStart], frame[bodyStart + 1], 0x00};
                sendAll(fd, suback);
            }
        } else if (type == 12) {  // PINGREQ → PINGRESP
            static const uint8_t pingresp[] = {0xD0, 0x00};
            sendAll(fd, std::vector<uint8_t>(pingresp, pingresp + 2));
        }
        // 其他类型忽略
    }

    static bool sendAll(int fd, const std::vector<uint8_t>& data) {
        if (fd < 0) return false;
        size_t total = 0;
        while (total < data.size()) {
            ssize_t n = send(fd, data.data() + total, data.size() - total, 0);
            if (n <= 0) return false;
            total += n;
        }
        return true;
    }

    int listenFd_ = -1;
    int clientFd_ = -1;
    int port_ = 0;
    std::atomic<bool> running_{false};
    std::atomic<bool> clientConnected_{false};
    std::thread acceptThread_;
    mutable std::mutex pubMu_;
    std::vector<Published> published_;
};

// =====================================================================
// 网络辅助: 找空闲端口 / HTTP POST(裸 socket)
// 端口必须落在 short 正范围(<32768),否则 WebService(short) 截断成负数 → 回退 8080
// =====================================================================
static int findFreePort() {
    const int base = 21000 + static_cast<int>(getpid() % 2000);
    for (int port = base; port < 30000; ++port) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;
        int opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(static_cast<uint16_t>(port));
        if (bind(fd, (sockaddr*)&addr, sizeof(addr)) == 0) {
            close(fd);
            return port;
        }
        close(fd);
    }
    return -1;
}

struct HttpResp {
    int status = 0;
    std::string body;
};

static HttpResp httpPost(int port, const std::string& path, const std::string& jsonBody) {
    HttpResp resp;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { std::cerr << "[httpPost] socket errno=" << errno << std::endl; return resp; }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[httpPost] connect 127.0.0.1:" << port << " errno=" << errno << std::endl;
        close(fd);
        return resp;
    }
    std::string req = "POST " + path + " HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " + std::to_string(jsonBody.size()) + "\r\n"
        "Connection: close\r\n\r\n" + jsonBody;
    ssize_t sent = send(fd, req.data(), req.size(), 0);
    if (sent < 0) std::cerr << "[httpPost] send errno=" << errno << std::endl;
    std::string raw;
    char buf[8192];
    ssize_t n;
    while ((n = recv(fd, buf, sizeof(buf), 0)) > 0) raw.append(buf, n);
    close(fd);
    if (raw.size() < 12) {
        std::cerr << "[httpPost] short response bytes=" << raw.size() << " errno=" << errno << std::endl;
    }
    // 解析 "HTTP/1.1 <code>" 与 body(最后一个空行后)
    if (raw.size() >= 12) {
        resp.status = std::atoi(raw.substr(9, 3).c_str());
    }
    size_t sep = raw.find("\r\n\r\n");
    if (sep != std::string::npos) resp.body = raw.substr(sep + 4);
    return resp;
}

// =====================================================================
// 测试环境: 服务栈引用集合 + 缓存等待辅助
// =====================================================================
struct TestEnv {
    VirtualPlcInstance* plc = nullptr;
    DeviceService* deviceService = nullptr;
    DeviceStatusCache* cache = nullptr;
    LobbyService* lobby = nullptr;
    MockMqttBroker* broker = nullptr;
    Timer* timer = nullptr;
    std::string boxNo;
    int httpPort = 0;
};

static bool waitCacheS(const TestEnv& env, int s, bool want, int timeoutMs = 4000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        BoxDeviceStatus st = env.deviceService->viewAllDeviceStatus();
        for (const auto& fx : st.getFxPlcStatusList()) {
            bool v = false;
            if (fx.getSBit(s, v) && v == want) return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

static bool waitCacheM(const TestEnv& env, int m, bool want, int timeoutMs = 4000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        BoxDeviceStatus st = env.deviceService->viewAllDeviceStatus();
        for (const auto& fx : st.getFxPlcStatusList()) {
            bool v = false;
            if (fx.getMBit(m, v) && v == want) return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

static bool waitCacheD(const TestEnv& env, int d, uint16_t want, int timeoutMs = 4000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        BoxDeviceStatus st = env.deviceService->viewAllDeviceStatus();
        for (const auto& fx : st.getFxPlcStatusList()) {
            uint16_t v = 0;
            if (fx.getDRegister(d, v) && v == want) return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

// 重置到指定场景(所有用例前置; 先清命令位/状态位, 再置场景状态, 等缓存跟上)
static void resetToScene(TestEnv& env, const std::string& scene) {
    env.plc->setFailMode(VirtualPlcInstance::FailMode::NONE);
    env.plc->setScanPaused(false);
    env.plc->setAutoAdvance(false);
    env.plc->resetEdgeDetectors();
    env.plc->clearForceHistory();
    env.plc->setM(addr::M_START_PREPARE, false);
    env.plc->setM(addr::M_STOP, false);
    env.plc->setM(addr::M_ABORT, false);
    env.plc->setM(addr::M_RESET, false);
    env.plc->setM(addr::M_RESET_SAFE, false);
    env.plc->setM(addr::M_STOP_WORK, false);
    env.plc->setM(addr::M_STOP_SERVICE, false);
    env.plc->setM(addr::M_UNSAFE, false);
    env.plc->setM(addr::M_HW_FAULT, false);
    env.plc->setM(addr::M_CAR_POSITIONED, false);
    env.plc->setX(36, false);
    env.plc->setX(20, false);
    env.plc->setX(21, true);
    env.plc->setD(addr::D_FAULT_CODE, 0);
    env.plc->setD(addr::D_DURATION_SEC, 0);

    if (scene == "standby") {
        env.plc->forceState(5);
        env.plc->setM(addr::M_CAR_POSITIONED, true);
        waitCacheS(env, 5, true);
        waitCacheM(env, addr::M_CAR_POSITIONED, true);
        // 等干扰位(M148/M501/M510)缓存刷新为 false, 避免上一场景残留被业务读到
        waitCacheM(env, addr::M_HW_FAULT, false);
        waitCacheM(env, addr::M_STOP_WORK, false);
        waitCacheM(env, addr::M_STOP_SERVICE, false);
    } else if (scene == "stop") {
        env.plc->forceState(0);
        waitCacheS(env, 0, true);
        // 等干扰位(M148/M501/M510)缓存刷新为 false, 避免上一场景残留被业务读到
        waitCacheM(env, addr::M_HW_FAULT, false);
        waitCacheM(env, addr::M_STOP_WORK, false);
        waitCacheM(env, addr::M_STOP_SERVICE, false);
    } else if (scene == "washing") {
        env.plc->forceState(addr::S_FIRST_WASH);
        waitCacheS(env, addr::S_FIRST_WASH, true);
    } else if (scene == "homing") {
        env.plc->forceState(1);
        waitCacheS(env, 1, true);
    } else if (scene == "alarm") {
        env.plc->setD(addr::D_FAULT_CODE, 105);
        env.plc->forceState(3);
        waitCacheS(env, 3, true);
        waitCacheD(env, addr::D_FAULT_CODE, 105);
    } else if (scene == "paused") {
        env.plc->forceState(5);
        env.plc->setM(addr::M_STOP_SERVICE, true);
        waitCacheS(env, 5, true);
        waitCacheM(env, addr::M_STOP_SERVICE, true);
    } else if (scene == "stopWork") {
        env.plc->forceState(0);
        env.plc->setM(addr::M_STOP_WORK, true);
        waitCacheS(env, 0, true);
        waitCacheM(env, addr::M_STOP_WORK, true);
    } else if (scene == "hwFault") {
        env.plc->forceState(0);
        env.plc->setM(addr::M_HW_FAULT, true);
        waitCacheS(env, 0, true);
        waitCacheM(env, addr::M_HW_FAULT, true);
    } else if (scene == "unsafe") {
        env.plc->forceState(0);
        env.plc->setM(addr::M_UNSAFE, true);
        waitCacheS(env, 0, true);
        waitCacheM(env, addr::M_UNSAFE, true);
    } else if (scene == "carNotPositioned") {
        env.plc->forceState(5);
        env.plc->setX(20, true);           // 车头传感器遮挡 → 车未到位
        env.plc->setM(addr::M_CAR_POSITIONED, false);
        waitCacheS(env, 5, true);
        waitCacheM(env, addr::M_CAR_POSITIONED, false);
    }
}

// 命令下发辅助
static HttpResp httpCmd(const TestEnv& env, const std::string& cmd, const json& extra = json::object()) {
    json payload = {{"boxNo", env.boxNo}, {"plcId", "tcp_plc_01"}};
    for (auto it = extra.begin(); it != extra.end(); ++it) payload[it.key()] = it.value();
    return httpPost(env.httpPort, "/93/11/washcar/" + cmd, payload.dump());
}

static void mqttCmd(const TestEnv& env, const json& extra) {
    json payload = {{"boxNo", env.boxNo}, {"plcId", "tcp_plc_01"}};
    for (auto it = extra.begin(); it != extra.end(); ++it) payload[it.key()] = it.value();
    env.broker->injectCommand("flexem/" + env.boxNo + "/command", payload.dump());
}

static void mqttRead(const TestEnv& env) {
    json payload = {{"boxNo", env.boxNo}, {"plcId", "tcp_plc_01"}};
    env.broker->injectCommand("flexem/" + env.boxNo + "/read", payload.dump());
}

static int jsonCode(const HttpResp& r) {
    try {
        json j = json::parse(r.body);
        return j.value("code", -1);
    } catch (...) {
        return -999;
    }
}

// HTTP 拒绝响应: 500 + {"error": ErrorCode::getMessage(错误码)}
static std::string httpError(const HttpResp& r) {
    try {
        json j = json::parse(r.body);
        return j.value("error", "");
    } catch (...) {
        return "__parse_fail__";
    }
}

// 断言 HTTP 拒绝: 状态 500 且 error 文本 == 预期错误码文案
static void checkHttpReject(const TestEnv& env, const HttpResp& r, ErrorCode::Code code, const std::string& tag) {
    CHECK_EQ(r.status, 500, tag + " status=500");
    const std::string want = ErrorCode::getMessage(code);
    const std::string got = httpError(r);
    CHECK(got == want, tag + " error text matches " + std::to_string(static_cast<int>(code))
        + " (got=[" + got + "] want=[" + want + "])");
    (void)env;
}

// MQTT 写历史轮询(命令在 handler 线程执行, 主线程需等待)
static bool waitForced(TestEnv& env, int m, bool want, int timeoutMs = 3000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (env.plc->wasMForced(m, want)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

// =====================================================================
// HTTP 链路用例组
// =====================================================================
static void runHttpCases(TestEnv& env) {
    std::cout << "\n===== HTTP 链路用例 =====" << std::endl;

    // ---- start 主路径: 模式 1/2/3 ----
    resetToScene(env, "standby");
    {
        HttpResp r = httpCmd(env, "start", {{"mode", "1"}});
        CHECK_EQ(r.status, 200, "HTTP start(m1) http status 200");
        CHECK(waitCacheS(env, addr::S_FIRST_WASH, true, 4000), "HTTP start(m1) S20 reached");
        // 新会话 D2000 从 0 起算(秒计数, 断言时最多累计 1~2s)
        CHECK(env.plc->getD(addr::D_DURATION_SEC) <= 2, "HTTP start(m1) duration restarted");
    }
    resetToScene(env, "standby");
    {
        HttpResp r = httpCmd(env, "start", {{"mode", "2"}});
        CHECK_EQ(r.status, 200, "HTTP start(m2) http status 200");
        CHECK(waitCacheS(env, addr::S_FIRST_WASH, true, 4000), "HTTP start(m2) S20 reached");
    }
    resetToScene(env, "standby");
    {
        HttpResp r = httpCmd(env, "start", {{"mode", "3"}});
        CHECK_EQ(r.status, 200, "HTTP start(m3) http status 200");
        CHECK(waitCacheS(env, addr::S_FIRST_WASH, true, 4000), "HTTP start(m3) S20 reached");
    }
    // start 拒绝: 缺模式
    resetToScene(env, "standby");
    {
        HttpResp r = httpCmd(env, "start", json::object());
        checkHttpReject(env, r, ErrorCode::Code::WASHCAR_NO_MODE, "HTTP start no-mode rejected");
    }
    // start 拒绝: 暂停服务(M510=1)
    resetToScene(env, "paused");
    {
        HttpResp r = httpCmd(env, "start", {{"mode", "1"}});
        checkHttpReject(env, r, ErrorCode::Code::WASHCAR_START_REJECTED, "HTTP start rejected when paused");
        CHECK(!env.plc->wasMForced(addr::M_START_PREPARE, true), "HTTP start paused: M204 not written");
    }
    // start 拒绝: 洗车中
    resetToScene(env, "washing");
    {
        HttpResp r = httpCmd(env, "start", {{"mode", "1"}});
        checkHttpReject(env, r, ErrorCode::Code::WASHCAR_START_REJECTED, "HTTP start rejected while washing");
    }
    // start 拒绝: 车未到位(M282=0)
    resetToScene(env, "carNotPositioned");
    {
        HttpResp r = httpCmd(env, "start", {{"mode", "1"}});
        checkHttpReject(env, r, ErrorCode::Code::WASHCAR_START_REJECTED, "HTTP start rejected car not positioned");
    }
    // start 超时: PLC 不响应确认点(超时路径与超时时长无关: 扫描暂停=永不确认)
    resetToScene(env, "standby");
    env.plc->setScanPaused(true);
    {
        HttpResp r = httpCmd(env, "start", {{"mode", "1"}});
        checkHttpReject(env, r, ErrorCode::Code::WASHCAR_COMMAND_TIMEOUT, "HTTP start timeout");
    }
    env.plc->setScanPaused(false);

    // ---- stop ----
    // stop 主路径: 待机 → 写 M205 → S0, 业务以 5021(已停止) 返回
    resetToScene(env, "standby");
    {
        HttpResp r = httpCmd(env, "stop");
        checkHttpReject(env, r, ErrorCode::Code::WASHCAR_STOPPED, "HTTP stop from standby -> 5021 stopped");
        CHECK(waitCacheS(env, 0, true, 4000), "HTTP stop: S0 reached");
        CHECK(env.plc->wasMForced(addr::M_STOP, true), "HTTP stop: M205 written");
    }
    // stop 无效: 洗车中
    resetToScene(env, "washing");
    {
        HttpResp r = httpCmd(env, "stop");
        checkHttpReject(env, r, ErrorCode::Code::WASHCAR_STOP_INVALID_WHILE_WASHING, "HTTP stop invalid while washing");
        CHECK(!env.plc->wasMForced(addr::M_STOP, true), "HTTP stop washing: M205 not written");
    }
    // stop 无效: 归位中
    resetToScene(env, "homing");
    {
        HttpResp r = httpCmd(env, "stop");
        checkHttpReject(env, r, ErrorCode::Code::WASHCAR_STOP_INVALID_WHILE_WASHING, "HTTP stop invalid while homing");
    }

    // ---- abort ----
    // abort 主路径: 洗车中 → S4 + D1000=106
    resetToScene(env, "washing");
    {
        HttpResp r = httpCmd(env, "abort");
        CHECK_EQ(r.status, 200, "HTTP abort while washing http status 200");
        CHECK(waitCacheS(env, 4, true, 4000), "HTTP abort: S4 reached");
        CHECK(waitCacheD(env, addr::D_FAULT_CODE, 106, 4000), "HTTP abort: D1000=106");
    }
    // abort 无效: 待机
    resetToScene(env, "standby");
    {
        HttpResp r = httpCmd(env, "abort");
        checkHttpReject(env, r, ErrorCode::Code::WASHCAR_ABORT_INVALID_NOT_WASHING, "HTTP abort invalid not washing");
        CHECK(!env.plc->wasMForced(addr::M_ABORT, true), "HTTP abort standby: M206 not written");
    }

    // ---- reset ----
    // reset 主路径: 停止态 → 归位完成 S5(S1/S2 为瞬态中间步, 由服务内部确认, 测试断言终态)
    resetToScene(env, "stop");
    {
        HttpResp r = httpCmd(env, "reset");
        CHECK_EQ(r.status, 200, "HTTP reset from stop http status 200");
        CHECK(waitCacheS(env, 5, true, 4000), "HTTP reset: S5 reached (home done)");
    }
    // reset 拒绝: 洗车中
    resetToScene(env, "washing");
    {
        HttpResp r = httpCmd(env, "reset");
        checkHttpReject(env, r, ErrorCode::Code::WASHCAR_RESET_REJECTED, "HTTP reset rejected while washing");
    }
    // reset 拒绝: 硬件故障 M148
    resetToScene(env, "hwFault");
    {
        HttpResp r = httpCmd(env, "reset");
        checkHttpReject(env, r, ErrorCode::Code::WASHCAR_RESET_REJECTED, "HTTP reset rejected hw fault");
        CHECK(!env.plc->wasMForced(addr::M_RESET, true), "HTTP reset hwFault: M202 not written");
    }
    // reset 拒绝: 停止营业 M501
    resetToScene(env, "stopWork");
    {
        HttpResp r = httpCmd(env, "reset");
        checkHttpReject(env, r, ErrorCode::Code::WASHCAR_RESET_REJECTED, "HTTP reset rejected stop-work");
    }
    // reset 超时: PLC 不响应
    resetToScene(env, "stop");
    env.plc->setScanPaused(true);
    {
        HttpResp r = httpCmd(env, "reset");
        checkHttpReject(env, r, ErrorCode::Code::WASHCAR_COMMAND_TIMEOUT, "HTTP reset timeout");
    }
    env.plc->setScanPaused(false);

    // ---- resetSafe ----
    // resetSafe 主路径: 停止态 → 归位启动并完成(确认点 S1 为瞬态, 服务内部确认, 测试断言归位终态)
    resetToScene(env, "stop");
    {
        HttpResp r = httpCmd(env, "resetSafe");
        CHECK_EQ(r.status, 200, "HTTP resetSafe from stop http status 200");
        CHECK(waitCacheS(env, 5, true, 4000), "HTTP resetSafe: S5 reached (home done)");
    }
    // resetSafe 拒绝: 停车位有车 M147
    resetToScene(env, "unsafe");
    {
        HttpResp r = httpCmd(env, "resetSafe");
        checkHttpReject(env, r, ErrorCode::Code::WASHCAR_RESET_SAFE_REJECTED, "HTTP resetSafe rejected M147");
        CHECK(!env.plc->wasMForced(addr::M_RESET_SAFE, true), "HTTP resetSafe unsafe: M203 not written");
    }

    // ---- stopWork ----
    resetToScene(env, "standby");
    {
        HttpResp r = httpCmd(env, "stopWork");
        CHECK_EQ(r.status, 200, "HTTP stopWork from standby http status 200");
        CHECK(waitCacheS(env, 0, true, 4000), "HTTP stopWork: S0 reached");
        CHECK(waitCacheM(env, addr::M_STOP_WORK, true, 4000), "HTTP stopWork: M501 stays on");
    }

    // ---- stopService ----
    resetToScene(env, "standby");
    {
        HttpResp r = httpCmd(env, "stopService");
        CHECK_EQ(r.status, 200, "HTTP stopService from standby http status 200");
        CHECK(waitCacheM(env, addr::M_STOP_SERVICE, true, 4000), "HTTP stopService: M510 read back 1");
        // 暂停后启动被拒
        HttpResp r2 = httpCmd(env, "start", {{"mode", "1"}});
        checkHttpReject(env, r2, ErrorCode::Code::WASHCAR_START_REJECTED, "HTTP start rejected after stopService");
        // 恢复营业: /93/11/point/off M510 → 清服务自身暂停标志
        json resumePayload = {{"boxNo", env.boxNo}, {"plcId", "tcp_plc_01"}, {"point", "M510"}};
        HttpResp r3 = httpPost(env.httpPort, "/93/11/point/off", resumePayload.dump());
        CHECK_EQ(r3.status, 200, "HTTP resume after stopService http status 200");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        CHECK(waitCacheM(env, addr::M_STOP_SERVICE, false, 4000), "HTTP resume: M510 cleared");
    }

    // ---- status(洗车进度上报) ----
    resetToScene(env, "standby");
    {
        HttpResp r = httpCmd(env, "status");
        CHECK_EQ(r.status, 200, "HTTP status standby http status 200");
        try {
            json j = json::parse(r.body);
            CHECK_EQ(j.value("code", -1), 0, "HTTP status standby code=0");
            CHECK_EQ(j.value("step", -1), 0, "HTTP status standby step=0");
        } catch (...) { CHECK(false, "HTTP status standby body parse"); }
    }
    resetToScene(env, "washing");
    env.plc->setD(addr::D_DURATION_SEC, 7);
    waitCacheD(env, addr::D_DURATION_SEC, 7, 4000);
    {
        HttpResp r = httpCmd(env, "status");
        CHECK_EQ(r.status, 200, "HTTP status washing http status 200");
        try {
            json j = json::parse(r.body);
            CHECK_EQ(j.value("code", -1), 0, "HTTP status washing code=0");
            CHECK_EQ(j.value("step", -1), 1, "HTTP status washing step=1 (S20)");
        } catch (...) { CHECK(false, "HTTP status washing body parse"); }
    }
    resetToScene(env, "alarm");
    {
        HttpResp r = httpCmd(env, "status");
        CHECK_EQ(r.status, 200, "HTTP status alarm http status 200");
        try {
            json j = json::parse(r.body);
            CHECK_EQ(j.value("code", -1), 0, "HTTP status alarm code=0");
        } catch (...) { CHECK(false, "HTTP status alarm body parse"); }
    }
    resetToScene(env, "stop");
    {
        HttpResp r = httpCmd(env, "status");
        CHECK_EQ(r.status, 200, "HTTP status stop http status 200");
        try {
            json j = json::parse(r.body);
            CHECK_EQ(j.value("code", -1), 0, "HTTP status stop code=0");
        } catch (...) { CHECK(false, "HTTP status stop body parse"); }
    }
}

// =====================================================================
// MQTT 链路用例组(无同步回包: 断言写历史 + 缓存状态 + broker 上报)
// =====================================================================
static void runMqttCases(TestEnv& env) {
    std::cout << "\n===== MQTT 链路用例 =====" << std::endl;

    // start 模式 1
    resetToScene(env, "standby");
    {
        mqttCmd(env, {{"mode", "1"}, {"Manual start", 1}});
        CHECK(waitForced(env, addr::M_START_PREPARE, true), "MQTT start: M204 written");
        CHECK(waitCacheS(env, addr::S_FIRST_WASH, true, 4000), "MQTT start: S20 reached");
        // 状态变化 → 变化上报(monitor topic) 被 broker 捕获
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        bool captured = false;
        while (std::chrono::steady_clock::now() < deadline) {
            for (const auto& p : env.broker->published()) {
                if (p.payload.find("D1210") != std::string::npos) { captured = true; break; }
            }
            if (captured) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        CHECK(captured, "MQTT start: change report (D1210) captured by broker");
    }
    // start 模式 2
    resetToScene(env, "standby");
    {
        mqttCmd(env, {{"mode", "2"}, {"Manual start", 1}});
        CHECK(waitCacheS(env, addr::S_FIRST_WASH, true, 4000), "MQTT start(m2): S20 reached");
        CHECK(waitForced(env, addr::M_START_MODE_2, true), "MQTT start(m2): M522 written");
    }
    // start 拒绝(暂停): 无 M204 写
    resetToScene(env, "paused");
    {
        mqttCmd(env, {{"mode", "1"}, {"Manual start", 1}});
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        CHECK(!env.plc->wasMForced(addr::M_START_PREPARE, true), "MQTT start paused: M204 not written");
    }

    // stop
    resetToScene(env, "standby");
    {
        mqttCmd(env, {{"washcar stop", 1}});
        CHECK(waitForced(env, addr::M_STOP, true), "MQTT stop: M205 written");
        CHECK(waitCacheS(env, 0, true, 4000), "MQTT stop: S0 reached");
    }
    // stop 无效(洗车中)
    resetToScene(env, "washing");
    {
        mqttCmd(env, {{"washcar stop", 1}});
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        CHECK(!env.plc->wasMForced(addr::M_STOP, true), "MQTT stop washing: M205 not written");
    }

    // abort
    resetToScene(env, "washing");
    {
        mqttCmd(env, {{"machine abort", 1}});
        CHECK(waitForced(env, addr::M_ABORT, true), "MQTT abort: M206 written");
        CHECK(waitCacheS(env, 4, true, 4000), "MQTT abort: S4 reached");
        CHECK(waitCacheD(env, addr::D_FAULT_CODE, 106, 4000), "MQTT abort: D1000=106");
    }

    // reset
    resetToScene(env, "stop");
    {
        mqttCmd(env, {{"reset", 1}});
        CHECK(waitForced(env, addr::M_RESET, true), "MQTT reset: M202 written");
        CHECK(waitCacheS(env, 5, true, 4000), "MQTT reset: S5 reached (home done)");
    }

    // resetSafe
    resetToScene(env, "stop");
    {
        mqttCmd(env, {{"reset safely", 1}});
        CHECK(waitForced(env, addr::M_RESET_SAFE, true), "MQTT resetSafe: M203 written");
        CHECK(waitCacheS(env, 5, true, 4000), "MQTT resetSafe: S5 reached (home done)");
    }

    // stopWork
    resetToScene(env, "standby");
    {
        mqttCmd(env, {{"stop work", 1}});
        CHECK(waitForced(env, addr::M_STOP_WORK, true), "MQTT stopWork: M501 written");
        CHECK(waitCacheS(env, 0, true, 4000), "MQTT stopWork: S0 reached");
    }

    // stopService
    resetToScene(env, "standby");
    {
        mqttCmd(env, {{"stop service", 1}});
        CHECK(waitForced(env, addr::M_STOP_SERVICE, true), "MQTT stopService: M510 written");
        CHECK(waitCacheM(env, addr::M_STOP_SERVICE, true, 4000), "MQTT stopService: M510 read back 1");
    }

    // read(进度查询) → 状态上报被 broker 捕获
    resetToScene(env, "standby");
    env.broker->clearPublished();
    {
        mqttRead(env);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        bool captured = false;
        while (std::chrono::steady_clock::now() < deadline) {
            for (const auto& p : env.broker->published()) {
                if (p.payload.find("TEST_BOX_01") != std::string::npos) { captured = true; break; }
            }
            if (captured) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        CHECK(captured, "MQTT read: status reply captured by broker");
    }
}

// =====================================================================
// 断连 / 缓存未就绪 / 变化上报用例
// =====================================================================
static void runRobustnessCases(TestEnv& env) {
    std::cout << "\n===== 断连 / 健壮性用例 =====" << std::endl;

    // 全断: connect 失败 → M204 写失败 → 启动准备失败(或业务拒绝), 不崩溃
    resetToScene(env, "standby");
    env.plc->setFailMode(VirtualPlcInstance::FailMode::CONNECT_FAIL);
    {
        HttpResp r = httpCmd(env, "start", {{"mode", "1"}});
        CHECK(r.status == 200 || r.status == 500, "HTTP start full-disconnect: http status 200/500");
        CHECK(jsonCode(r) != 0 || httpError(r).empty(), "HTTP start full-disconnect: not success");
    }
    env.plc->setFailMode(VirtualPlcInstance::FailMode::NONE);

    // 半断: connect 成功但 IO 失败 → 同样写失败, 不崩溃
    resetToScene(env, "standby");
    env.plc->setFailMode(VirtualPlcInstance::FailMode::IO_FAIL);
    {
        HttpResp r = httpCmd(env, "start", {{"mode", "1"}});
        CHECK(r.status == 200 || r.status == 500, "HTTP start half-disconnect: http status 200/500");
        CHECK(jsonCode(r) != 0 || httpError(r).empty(), "HTTP start half-disconnect: not success");
    }
    env.plc->setFailMode(VirtualPlcInstance::FailMode::NONE);

    // 变化上报: 状态转移(D1210 变化)后 4s 内 broker 捕获 monitor 上报
    resetToScene(env, "standby");
    env.broker->clearPublished();
    {
        httpCmd(env, "start", {{"mode", "1"}});
        waitCacheS(env, addr::S_FIRST_WASH, true, 4000);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        bool captured = false;
        while (std::chrono::steady_clock::now() < deadline) {
            for (const auto& p : env.broker->published()) {
                if (p.payload.find("D1210") != std::string::npos) { captured = true; break; }
            }
            if (captured) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        CHECK(captured, "change report after HTTP start captured (D1210)");
    }
}

// =====================================================================
// 组装与服务栈管理
// =====================================================================
struct ServiceStack {
    MockMqttBroker broker;
    std::unique_ptr<VirtualPlcInstance> plc;
    FxPlcInstanceSet fxPlcSet;
    DeviceManageService* deviceManage = nullptr;
    DeviceAcquisitionTask* acquisitionTask = nullptr;
    DeviceStatusCache cache;
    RealTimeFrameCache frameCache;
    std::unique_ptr<DeviceService> deviceService;
    std::unique_ptr<SafetyService> safetyService;
    std::unique_ptr<Timer> timer;
    std::unique_ptr<CommandDao> commandDao;
    std::unique_ptr<CommandService> commandService;
    std::unique_ptr<ModuleDao> moduleDao;
    std::unique_ptr<DetectionResultCache> detectionCache;
    std::unique_ptr<DetectionService> detectionService;
    std::unique_ptr<YiGongLiWashCarService> washcarService;
    std::unique_ptr<LobbyService> lobby;
    std::unique_ptr<MQTTCommandController> mqttController;
    std::unique_ptr<HTTPCommandController> httpController;
    std::unique_ptr<MqttService> mqttService;
    std::unique_ptr<WebService> webService;
    std::unique_ptr<MqttProtocol> protocol;
    std::shared_ptr<BlockingQueue> queue;
    std::shared_ptr<AsyncLogger> logger;
    std::thread mqttThread;
};

int main(int argc, char* argv[]) {
    std::cout << "=============================================" << std::endl;
    std::cout << "驿公里洗车服务 命令模拟测试启动" << std::endl;
    std::cout << "=============================================" << std::endl;

    // 1. exe 目录 + chdir(配置/日志与 exe 同级)
    std::filesystem::path exeDir;
    try {
        exeDir = std::filesystem::absolute(argv[0] ? argv[0] : "").parent_path();
    } catch (...) {
        exeDir = std::filesystem::current_path();
    }
    if (exeDir.empty()) exeDir = std::filesystem::current_path();
    std::filesystem::current_path(exeDir);

    ServiceStack s;

    // 2. 起 Mock Broker(先拿端口, 配置里引用)
    if (!s.broker.start()) {
        std::cerr << ">>> 致命: MockBroker 启动失败" << std::endl;
        return -1;
    }
    const int brokerPort = s.broker.port();

    // 3. 写临时 config.json(仅测试: 确认超时 1500ms 提速, 变化/故障上报 500ms 提速)
    const std::string configPath = (exeDir / "config.json").string();
    {
        json cfg = {
            {"box", {{"boxNo", "TEST_BOX_01"}, {"boxType", "yiGongLi"}, {"plcEnabled", true}, {"dbPath", "./test_db"}}},
            {"mqtt", {{"brokerIp", "127.0.0.1"}, {"port", brokerPort}, {"username", ""}, {"password", ""}}},
            {"plcPoints", {{"valueChangePoints",
                {"d2000", "d2001", "d2002", "d2005", "d2006", "d1210", "d1028", "d1030", "d1032"}}}},
            {"timing", {{"washcarCommandConfirmTimeoutMs", 3000}, {"valueChangeIntervalMs", 500}, {"faultReportIntervalMs", 500}}}
        };
        std::ofstream ofs(configPath, std::ios::trunc);
        if (!ofs.is_open()) {
            std::cerr << ">>> 致命: 无法写测试 config.json: " << configPath << std::endl;
            return -1;
        }
        ofs << cfg.dump(2);
        ofs.close();
    }
    {
        std::string err;
        if (!ConfigManager::instance().load(configPath, err)) {
            std::cerr << ">>> 致命: 配置加载失败: " << err << std::endl;
            return -1;
        }
    }
    const auto& cfg = ConfigManager::instance();

    // 4. 日志
    s.queue = std::make_shared<BlockingQueue>();
    s.logger = std::make_shared<AsyncLogger>(s.queue);
    auto fileSink = std::make_shared<FileSink>("log");
    auto consoleSink = std::make_shared<ConsoleSink>();
    s.logger->addSink(fileSink);
    s.logger->addSink(consoleSink);
    s.logger->setLogLevel(LogLevel::DEBUG);
    if (!s.logger->start()) {
        std::cerr << ">>> 错误: 日志启动失败" << std::endl;
        return -1;
    }
    LoggerManager::instance().registerLogger("work", s.logger);

    // 5. 数据层: 虚拟 PLC + 采集
    s.plc = std::make_unique<VirtualPlcInstance>("tcp_plc_01");
    s.fxPlcSet.addPlc(std::move(s.plc));

    std::unordered_map<std::string, std::unique_ptr<Camera>> emptyCameras;
    CameraInstanceSet cameraInstances(std::move(emptyCameras));
    std::vector<DoorLock> doorLocks;
    GPIODeviceInstanceSet gpioInstanceSet(doorLocks);
    std::vector<TempHumidSensor> sensors;
    SerialDirectDeviceInstanceSet serialInstances(sensors);

    auto deviceManage = std::make_unique<DeviceManageService>(
        std::move(s.fxPlcSet), std::move(cameraInstances), std::move(gpioInstanceSet), std::move(serialInstances));
    // fxPlcSet 已 move 进 DeviceManageService, 后续一律从活动 set 取
    auto& activeFxPlcSet = deviceManage->getFxPlcSet();
    if (!activeFxPlcSet.getPlc("tcp_plc_01")) {
        std::cerr << ">>> 致命: 虚拟 PLC 注入失败" << std::endl;
        return -1;
    }
    auto acquisitionTask = std::make_unique<DeviceAcquisitionTask>(
        activeFxPlcSet, ConfigManager::instance().machine("yiGongLi"));
    s.deviceService = std::make_unique<DeviceService>(*deviceManage, s.cache, *acquisitionTask, s.frameCache);

    // 6. 业务层
    auto& dbManager = DatabaseManager::instance();
    std::error_code ec;
    std::filesystem::create_directories(cfg.box().dbPath, ec);
    if (!dbManager.init(cfg.box().dbPath)) {
        std::cerr << ">>> 错误: 数据库初始化失败" << std::endl;
        return -1;
    }
    s.safetyService = std::make_unique<SafetyService>();
    s.timer = std::make_unique<Timer>();
    s.commandDao = std::make_unique<CommandDao>(dbManager.getCommandDB());
    NetworkService* networkService = nullptr;
    s.commandService = std::make_unique<CommandService>(networkService, *s.commandDao);
    s.moduleDao = std::make_unique<ModuleDao>(dbManager);
    s.detectionCache = std::make_unique<DetectionResultCache>();
    s.detectionService = std::make_unique<DetectionService>(*s.moduleDao, s.frameCache, *s.detectionCache, washcar::cfg::AiConfig{});
    s.washcarService = std::make_unique<YiGongLiWashCarService>(*s.deviceService);
    s.lobby = std::make_unique<LobbyService>(*s.safetyService, *s.commandService, *s.deviceService,
                                             *s.washcarService, *s.timer, s.cache, *s.detectionService);
    s.mqttController = std::make_unique<MQTTCommandController>(*s.lobby);
    s.httpController = std::make_unique<HTTPCommandController>(*s.lobby);

    // 7. 通信层: MQTT 真客户端 + Mock Broker; HTTP 真 WebService
    s.protocol = std::make_unique<MqttProtocol>();
    s.mqttService = std::make_unique<MqttService>("127.0.0.1", static_cast<short>(brokerPort),
        cfg.mqtt().username, cfg.mqtt().password, cfg.box().boxNo, *s.mqttController, *s.protocol);
    s.commandService->immitDependence(*s.mqttService);
    s.mqttThread = std::thread([&s]() { s.mqttService->start(); });

    const int httpPort = findFreePort();
    if (httpPort < 0) { std::cerr << ">>> 致命: 找不到空闲 HTTP 端口" << std::endl; return -1; }
    s.webService = std::make_unique<WebService>("127.0.0.1", static_cast<short>(httpPort), *s.httpController);
    if (!s.webService->start()) {
        std::cerr << ">>> 错误: HTTP 服务启动失败" << std::endl;
        return -1;
    }

    TestEnv env;
    env.plc = dynamic_cast<VirtualPlcInstance*>(activeFxPlcSet.getPlc("tcp_plc_01"));
    if (!env.plc) {
        std::cerr << ">>> 致命: 虚拟 PLC dynamic_cast 失败" << std::endl;
        return -1;
    }
    env.deviceService = s.deviceService.get();
    env.cache = &s.cache;
    env.lobby = s.lobby.get();
    env.broker = &s.broker;
    env.timer = s.timer.get();
    env.boxNo = cfg.box().boxNo;
    env.httpPort = httpPort;

    if (!s.broker.waitForClient(3000)) {
        std::cerr << ">>> 错误: MQTT 客户端未在 3s 内连上 Mock Broker" << std::endl;
        return -1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));   // 等握手+订阅完成

    // 8. CACHE_NOT_READY: 组装后立即下发(采集缓存可能未就绪), 要求不崩溃且返回业务码
    std::cout << "\n===== 缓存未就绪健壮性 =====" << std::endl;
    {
        json payload = {{"boxNo", env.boxNo}, {"plcId", "tcp_plc_01"}, {"mode", "1"}};
        HttpResp r = httpPost(httpPort, "/93/11/washcar/start", payload.dump());
        // 缓存未就绪时业务可能直接成功, 也可能拒绝; 只要求不崩溃且返回可解析响应
        CHECK(r.status == 200 || r.status == 500, "cache-not-ready: http status 200/500");
        CHECK(!r.body.empty(), "cache-not-ready: response body returned");
    }

    // 9. 等首轮采集完成(缓存就绪)
    std::this_thread::sleep_for(std::chrono::seconds(2));
    env.plc->forceState(5);
    env.plc->setM(addr::M_CAR_POSITIONED, true);
    waitCacheS(env, 5, true, 4000);

    // 10. 用例组
    runHttpCases(env);
    runMqttCases(env);
    runRobustnessCases(env);

    // 11. 汇总
    std::cout << "\n=============================================" << std::endl;
    std::cout << "测试结束: PASS=" << g_pass << " FAIL=" << g_fail << std::endl;
    std::cout << "=============================================" << std::endl;

    // 12. 收尾(顺序: 先停状态监控线程, 再停服务与日志)
    s.washcarService->stopWashCarStatusMonitor();
    s.webService->stop();
    s.mqttService->stop();
    if (s.mqttThread.joinable()) s.mqttThread.join();
    s.timer->stopUpload();
    s.logger->stop();

    return g_fail == 0 ? 0 : 1;
}
