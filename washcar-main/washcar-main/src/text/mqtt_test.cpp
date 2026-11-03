#include "business_layer/command/mqtt/mqtt_object.h"
#include "business_layer/command/mqtt/mqtt_service.h"
#include "business_layer/command/mqtt/network_service.h"
#include "business_layer/command/mqtt/protocol.h"
#include "business_layer/command/controller.h"
#include "business_layer/safety/safety_service.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <mutex>
#include <string>

class EmptyController : public IController {
public:
	// 实现纯虚函数，空逻辑
	HttpResponse handleHttp(const std::string& topic, const std::string& payload) override {
		return {}; // 根据HttpResponse类型返回默认值即可
	}

	void handleMqtt(const std::string& topic, const std::string& payload) override {
		// 空实现，什么都不处理
	}
};

int main() {
	class SafetyService safeservice;

    std::string username = "admin";//

    std::string password = "llx@123456";//

	std::string key = "KBWMO7N3762QW376";

	std::string brokerIp = "116.204.32.197";

	std::string boxNo = "123456";

	short port = 1883;
	EmptyController emptyController;
	// password = safeservice.aes_cbc_encrypt(password,key);
	MqttProtocol protocol;
	MqttService mqttService(brokerIp, port, username, password, boxNo, emptyController, protocol);

	mqttService.start();

	// 单元测试形态:连上 broker → 报 PASS 退出;超时/连接失败 → 报 FAIL 退出。
	// 10 秒硬上限,避免环境问题(网络不通)时永远卡住。
	const int kMaxWaitSec = 10;
	int elapsed = 0;
	while (!mqttService.isConnected() && elapsed < kMaxWaitSec) {
		std::this_thread::sleep_for(std::chrono::seconds(1));
		elapsed++;
	}

	if (mqttService.isConnected()) {
		std::cout << "[mqtt_test] PASS: connected to broker " << brokerIp
		          << ":" << port << " in " << elapsed << "s" << std::endl;
		return 0;
	}
	std::cerr << "[mqtt_test] FAIL: not connected to broker " << brokerIp
	          << ":" << port << " after " << kMaxWaitSec << "s" << std::endl;
	return 1;
}
