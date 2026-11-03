// fx_test_client.cpp — minimal FX3U PLC client for vplc L2 test
// 用法: ./fx_test_client <serialPort> <scenario>
//   scenario = "basic"   : 读 D142/M1/S0,force M0/M100/M500/M1023,然后读回验证
//   scenario = "fault"   : 读 X20/X32/X50 一组 + 13 条故障,验证响应正确
//   scenario = "forceX"  : 读 X45,force X45 on,读回应=on,再 force off,读回应=off
//   scenario = "all"     : 跑上面三个
//
// 退出码: 0=全过,1=某项失败,2=串口/连接失败
#include "data_layer/fx_plc/fx_plc_device.h"
#include "data_layer/fx_plc/fx_plc_instance.h"
#include <cstdio>
#include <cstdlib>
#include <string>

static int g_pass = 0, g_fail = 0;

#define EXPECT(cond, label) do { \
    if (cond) { g_pass++; printf("  [PASS] %s\n", label); } \
    else      { g_fail++; printf("  [FAIL] %s\n", label); } \
} while (0)

static void basicRead(FxPlcInstance& plc) {
    printf("[basic] read D142/M1/S0\n");
    uint16_t d142 = 0; EXPECT(plc.readD(142, d142), "readD D142");
    bool m1 = false;  EXPECT(plc.readM(1, m1), "readM M1 (self-check)");
    bool s0 = false;  EXPECT(plc.readS(0, s0), "readS S0 (stopped)");
    printf("  -> D142=%u M1=%d S0=%d\n", d142, (int)m1, (int)s0);
}

static void forceM(FxPlcInstance& plc) {
    printf("[forceM] M0=stop, M100=reset, M500=jog, M1023=test\n");
    int writes[4] = {0, 100, 500, 1023};
    for (int m : writes) {
        EXPECT(plc.forceM(m, true),  ("forceM M" + std::to_string(m) + " ON").c_str());
        bool v = false; EXPECT(plc.readM(m, v) && v, ("readM M" + std::to_string(m) + "==ON").c_str());
        EXPECT(plc.forceM(m, false), ("forceM M" + std::to_string(m) + " OFF").c_str());
        bool v2 = true; EXPECT(plc.readM(m, v2) && !v2, ("readM M" + std::to_string(m) + "==OFF").c_str());
    }
}

static void faultRead(FxPlcInstance& plc) {
    printf("[fault] read X group 2/3/5 (X20,X32-X37,X50)\n");
    int s2 = 0, s3 = 0, s5 = 0;
    EXPECT(plc.readX(2, s2) == true, "readX group 2");
    EXPECT(plc.readX(3, s3) == true, "readX group 3");
    EXPECT(plc.readX(5, s5) == true, "readX group 5");
    printf("  -> X[20-27]=%03o X[32-37]=%02o X[50-57]=%03o\n", s2, s3, s5);
    // 13 故障单点
    int faults[13] = {020, 032, 033, 034, 035, 036, 037, 040, 041, 042, 043, 047, 052};
    for (int x : faults) {
        bool s = true; EXPECT(plc.readXBit(x, s), ("readXBit X" + std::to_string(x)).c_str());
    }
}

static void forceX(FxPlcInstance& plc) {
    printf("[forceX] X45=start\n");
    EXPECT(plc.forceX(45, true),  "forceX X45 ON");
    bool s1 = false; EXPECT(plc.readXBit(45, s1) && s1, "readXBit X45==ON");
    EXPECT(plc.forceX(45, false), "forceX X45 OFF");
    bool s2 = true;  EXPECT(plc.readXBit(45, s2) && !s2, "readXBit X45==OFF");
}

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("usage: %s <serialPort> <basic|fault|forceX|forceM|all>...\n", argv[0]);
        return 2;
    }
    std::string port = argv[1];

    FxPlcDevice dev(/*plcId*/"vplc-test",
                     /*serialPort*/port,
                     /*baudRate*/9600,
                     /*dataBits*/7,
                     /*parityEven*/true,
                     /*stopBits*/1);

    FxPlcInstance plc(dev);
    if (!plc.connect()) { printf("[FAIL] connect %s\n", port.c_str()); return 2; }
    printf("[OK] connected to %s\n", port.c_str());

    // 支持一次跑多个 scenario(避免反复 connect/disconnect 导致 PTY termios 污染)
    for (int i = 2; i < argc; i++) {
        std::string scenario = argv[i];
        if (scenario == "basic" || scenario == "all") basicRead(plc);
        if (scenario == "fault" || scenario == "all") faultRead(plc);
        if (scenario == "forceX"|| scenario == "all") forceX(plc);
        if (scenario == "forceM"|| scenario == "all") forceM(plc);
    }

    plc.disconnect();
    printf("--- pass=%d fail=%d ---\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
