#pragma once

#include "data_layer/fx_plc/iplc_instance.h"
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class TcpPlcInstance : public IPlcInstance {
public:
	explicit TcpPlcInstance(const std::string& plcId,
							const std::string& host = "192.168.31.250",
							uint16_t port = 502,
							uint8_t unitId = 1,
							int timeoutMs = 2000);
	~TcpPlcInstance() override;

	bool connect() override;
	void disconnect() override;
	bool isConnected() const override;

	bool forceY(int yOctal, bool turnOn) override;
	bool forceX(int xOctal, bool turnOn) override;
	bool forceM(int mDecimal, bool turnOn) override;

	bool readD(int dNumber, uint16_t& value) override;
	bool readYBit(int yOctal, bool& state) override;
	bool readM(int mDecimal, bool& state) override;
	bool readXBit(int xOctal, bool& state) override;
	bool readX(int group, int& status8) override;
	bool readY(int group, int& status8) override;
	bool readS(int sDecimal, bool& state) override;

	const std::string& getPlcId() const override;

private:
	bool sendRequest(uint8_t functionCode,
					 const std::vector<uint8_t>& requestData,
					 std::vector<uint8_t>& responsePdu);
	bool readBit(uint8_t functionCode, uint16_t address, bool& state);
	bool writeBit(uint16_t address, bool state);
	bool readRegister(uint16_t address, uint16_t& value);
	bool writeRegister(uint16_t address, uint16_t value);
	bool readBits(uint8_t functionCode, uint16_t address, uint16_t quantity, int& status8);

	static uint16_t xAddress(int xOctal);
	static uint16_t yAddress(int yOctal);
	static uint16_t mAddress(int mDecimal);
	static uint16_t sAddress(int sDecimal);

	std::string plcId_;
	std::string host_;// IP
	uint16_t port_;// 端口
	uint8_t unitId_;// 从站地址
	int timeoutMs_ = 2000;   // 单个请求收发超时(ms)，来自 plcTcp.timeoutMs
	int socketFd_;
	bool connected_;
	uint16_t transactionId_;// 消息ID
	mutable std::mutex mutex_;
};
