#include "data_layer/fx_plc/tcp_plc_instance.h"
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>

static int g_pass = 0;
static int g_fail = 0;

// 简单的测试宏，输出测试结果
// 如果 condition 为 true，则计数 g_pass 并输出 [PASS] label
// 如果 condition 为 false，则计数 g_fail 并输出 [FAIL] label
#define EXPECT(condition, label) do { \
	if (condition) { ++g_pass; std::printf("[PASS] %s\n", label); } \
	else { ++g_fail; std::printf("[FAIL] %s\n", label); } \
} while (0)

int main(int argc, char** argv) {
	std::cout << "========== TcpPlcInstance 测试程序 ==========" << std::endl;

	const std::string host = argc > 1 ? argv[1] : "192.168.1.250";//"192.168.31.250";
	const uint16_t port = argc > 2
		? static_cast<uint16_t>(std::strtoul(argv[2], nullptr, 10))
		: 502;

	TcpPlcInstance plc("tcp-plc-test", host, port, 1);
	EXPECT(plc.getPlcId() == "tcp-plc-test", "PLC ID");

	if (!plc.connect()) {
		std::printf("[SKIP] 无法连接 Modbus TCP PLC %s:%u\n",
					host.c_str(), static_cast<unsigned>(port));
		return 2;
	}
	EXPECT(plc.isConnected(), "connect");

	uint16_t d142 = 0;
	EXPECT(plc.readD(142, d142), "read holding register D142");

	bool m1 = false;
	EXPECT(plc.readM(1, m1), "read coil M1");

	bool x44 = false;
	EXPECT(plc.readXBit(44, x44), "read discrete input X44");

	plc.disconnect();
	EXPECT(!plc.isConnected(), "disconnect");

	std::printf("D142=%u M1=%d X44=%d\n",
				static_cast<unsigned>(d142), static_cast<int>(m1), static_cast<int>(x44));
	std::printf("--- pass=%d fail=%d ---\n", g_pass, g_fail);
	return g_fail == 0 ? 0 : 1;
}
