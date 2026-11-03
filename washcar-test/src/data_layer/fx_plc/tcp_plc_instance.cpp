#include "data_layer/fx_plc/tcp_plc_instance.h"

#include "common/log/log_manager.h"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

bool sendAll(int fd, const uint8_t* data, size_t size) {
	size_t sent = 0;
	while (sent < size) {
		const ssize_t count = send(fd, data + sent, size - sent, 0);
		if (count <= 0) {
			return false;
		}
		sent += static_cast<size_t>(count);
	}
	return true;
}

bool receiveAll(int fd, uint8_t* data, size_t size) {
	size_t received = 0;
	while (received < size) {
		const ssize_t count = recv(fd, data + received, size - received, 0);
		if (count <= 0) {
			return false;
		}
		received += static_cast<size_t>(count);
	}
	return true;
}

uint16_t readU16(const uint8_t* data) {
	return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) | data[1]);
}

void appendU16(std::vector<uint8_t>& data, uint16_t value) {
	data.push_back(static_cast<uint8_t>(value >> 8));
	data.push_back(static_cast<uint8_t>(value & 0xff));
}

} // namespace

TcpPlcInstance::TcpPlcInstance(const std::string& plcId,
							   const std::string& host,
							   uint16_t port,
							   uint8_t unitId,
							   int timeoutMs)
	: plcId_(plcId),
	  host_(host),
	  port_(port),
	  unitId_(unitId),
	  timeoutMs_(timeoutMs > 0 ? timeoutMs : 2000),
	  socketFd_(-1),
	  connected_(false),
	  transactionId_(0) {}

TcpPlcInstance::~TcpPlcInstance() {
	disconnect();
}

bool TcpPlcInstance::connect() {
	std::lock_guard<std::mutex> lock(mutex_);
	if (connected_) {
		return true;
	}

	socketFd_ = socket(AF_INET, SOCK_STREAM, 0);
	if (socketFd_ < 0) {
		LOG_ERROR("FX PLC [" + plcId_ + "] 连接失败: " + std::string(std::strerror(errno)));
		std::cerr << "[PLC] " << plcId_ << " 连接失败：" << strerror(errno) << std::endl;
		return false;
	}

	// 收发超时走配置（plcTcp.timeoutMs，默认 2000ms）。
	// 驿公里是逐点请求，这个值直接决定"PLC 不应答时一轮采集要多久"：
	// 113 个点 × 超时值 = 最坏情况下的单轮耗时。
	timeval timeout{};
	timeout.tv_sec = timeoutMs_ / 1000;
	timeout.tv_usec = (timeoutMs_ % 1000) * 1000;
	setsockopt(socketFd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	setsockopt(socketFd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_port = htons(port_);
	if (inet_pton(AF_INET, host_.c_str(), &address.sin_addr) != 1 ||
		::connect(socketFd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
		LOG_ERROR("FX PLC [" + plcId_ + "] 连接失败: " + std::string(std::strerror(errno)));
		std::cerr << "[PLC] " << plcId_ << " 连接失败：" << strerror(errno) << std::endl;
		close(socketFd_);
		socketFd_ = -1;
		return false;
	}

	connected_ = true;
	LOG_INFO("FX PLC [" + plcId_ + "] 连接成功");
	return true;
}

void TcpPlcInstance::disconnect() {
	std::lock_guard<std::mutex> lock(mutex_);
	if (socketFd_ >= 0) {
		close(socketFd_);
		socketFd_ = -1;
	}
	connected_ = false;
}

bool TcpPlcInstance::isConnected() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return connected_;
}

bool TcpPlcInstance::sendRequest(uint8_t functionCode,
								 const std::vector<uint8_t>& requestData,
								 std::vector<uint8_t>& responsePdu) {
	const uint16_t transactionId = ++transactionId_;
	const uint16_t pduLength = static_cast<uint16_t>(1 + requestData.size());
	const uint16_t tcpLength = static_cast<uint16_t>(1 + pduLength);

	std::vector<uint8_t> request;
	request.reserve(7 + pduLength);
	appendU16(request, transactionId);
	appendU16(request, 0);
	appendU16(request, tcpLength);
	request.push_back(unitId_);
	request.push_back(functionCode);
	request.insert(request.end(), requestData.begin(), requestData.end());

	if (!sendAll(socketFd_, request.data(), request.size())) {
		std::cerr << "发送 Modbus TCP 请求失败" << std::endl;
		return false;
	}

	uint8_t header[7];
	if (!receiveAll(socketFd_, header, sizeof(header))) {
		std::cerr << "读取 Modbus TCP 响应头失败" << std::endl;
		return false;
	}
	if (readU16(header) != transactionId || readU16(header + 2) != 0 || header[6] != unitId_) {
		std::cerr << "Modbus TCP 响应头校验失败" << std::endl;
		return false;
	}

	const uint16_t responseLength = readU16(header + 4);
	if (responseLength < 2 || responseLength > 253) {
		std::cerr << "Modbus TCP 响应长度错误" << std::endl;
		return false;
	}

	std::vector<uint8_t> pdu(responseLength - 1);
	if (!receiveAll(socketFd_, pdu.data(), pdu.size())) {
		std::cerr << "读取 Modbus TCP 响应数据失败" << std::endl;
		return false;
	}
	// 功能码最高位置 1 表示 PLC 异常响应, pdu[1] 为 Modbus 异常码
	// 异常码含义(排查对照):
	//   1  Illegal Function     非法功能码: PLC 不支持该功能码, 例如对只读区用写功能码、请求的功能码未使能
	//   2  Illegal Data Address 非法数据地址: 地址超出软元件映射区, 最常见于软元件偏移算错
	//      (如 M 未加 8192、S 区起始地址不对)或 PLC 未映射该区
	//   3  Illegal Data Value   非法数据值: 请求数据不合法, 如读取点数超范围
	//   4  Slave Device Failure 从站设备故障: PLC 内部错误
	//   5  Acknowledge          已受理但处理中: 长任务类请求
	//   6  Slave Device Busy    从站忙: PLC 忙, 稍后重试
	//   7  Negative Acknowledge 否定应答: 编程命令被拒绝
	//   8  Memory Parity Error  存储奇偶校验错误
	//   10 Gateway Path Unavailable         网关路径不可用
	//   11 Gateway Target Failed to Respond 网关目标无响应
	// 本项目最常见: 2(地址/偏移错) 和 1(功能码错); 出现 2 优先核对软元件映射与偏移, 出现 1 核对功能码选择
	if ((pdu[0] & 0x80) != 0) {
		const uint8_t exceptionCode = pdu.size() > 1 ? pdu[1] : 0;
		LOG_WARNING("FX PLC [" + plcId_ + "] Modbus 异常响应: 功能码 "
			+ std::to_string(functionCode) + " 异常码 " + std::to_string(exceptionCode));
		return false;
	}
	if (pdu[0] != functionCode || pdu.size() < 2) {
		std::cerr << "Modbus TCP 响应功能码错误" << std::endl;
		return false;
	}

	responsePdu = std::move(pdu);
	return true;
}

bool TcpPlcInstance::readBit(uint8_t functionCode, uint16_t address, bool& state) {
	std::vector<uint8_t> request;
	appendU16(request, address);
	appendU16(request, 1);

	std::vector<uint8_t> response;
	if (!sendRequest(functionCode, request, response) || response.size() < 3 || response[1] < 1) {
		return false;
	}
	state = (response[2] & 0x01) != 0;
	return true;
}

bool TcpPlcInstance::writeBit(uint16_t address, bool state) {
	std::vector<uint8_t> request;
	appendU16(request, address);
	appendU16(request, state ? 0xff00 : 0x0000);// ON 或 OFF

	std::vector<uint8_t> response;
	return sendRequest(5, request, response) && response.size() == 5;
}

bool TcpPlcInstance::readRegister(uint16_t address, uint16_t& value) {
	std::vector<uint8_t> request;
	appendU16(request, address);
	appendU16(request, 1);

	std::vector<uint8_t> response;
	if (!sendRequest(3, request, response) || response.size() < 4 || response[1] != 2) {
		return false;
	}
	value = readU16(response.data() + 2);
	return true;
}

bool TcpPlcInstance::writeRegister(uint16_t address, uint16_t value) {
	std::vector<uint8_t> request;
	appendU16(request, address);
	appendU16(request, value);

	std::vector<uint8_t> response;
	return sendRequest(6, request, response) && response.size() == 5;
}

bool TcpPlcInstance::readBits(uint8_t functionCode,
							  uint16_t address,
							  uint16_t quantity,
							  int& status8) {
	std::vector<uint8_t> request;
	appendU16(request, address);
	appendU16(request, quantity);

	std::vector<uint8_t> response;
	if (!sendRequest(functionCode, request, response) || response.size() < 3 || response[1] < 1) {
		return false;
	}
	status8 = response[2];
	return true;
}

bool TcpPlcInstance::forceY(int yOctal, bool turnOn) {
	std::lock_guard<std::mutex> lock(mutex_);
	return connected_ && writeBit(yAddress(yOctal), turnOn);
}

bool TcpPlcInstance::forceX(int xOctal, bool turnOn) {
	std::lock_guard<std::mutex> lock(mutex_);
	return connected_ && writeBit(xAddress(xOctal), turnOn);
}

bool TcpPlcInstance::forceM(int mDecimal, bool turnOn) {
	std::lock_guard<std::mutex> lock(mutex_);
	return connected_ && writeBit(mAddress(mDecimal), turnOn);
}

bool TcpPlcInstance::readD(int dNumber, uint16_t& value) {
	std::lock_guard<std::mutex> lock(mutex_);
	return connected_ && readRegister(static_cast<uint16_t>(dNumber), value);
}

bool TcpPlcInstance::readYBit(int yOctal, bool& state) {
	std::lock_guard<std::mutex> lock(mutex_);
	return connected_ && readBit(1, yAddress(yOctal), state);
}

bool TcpPlcInstance::readM(int mDecimal, bool& state) {
	std::lock_guard<std::mutex> lock(mutex_);
	return connected_ && readBit(1, mAddress(mDecimal), state);
}

bool TcpPlcInstance::readXBit(int xOctal, bool& state) {
	std::lock_guard<std::mutex> lock(mutex_);
	return connected_ && readBit(2, xAddress(xOctal), state);
}

bool TcpPlcInstance::readX(int group, int& status8) {
	std::lock_guard<std::mutex> lock(mutex_);
	return connected_ && readBits(2, static_cast<uint16_t>(group * 8), 8, status8);
}

bool TcpPlcInstance::readY(int group, int& status8) {
	std::lock_guard<std::mutex> lock(mutex_);
	return connected_ && readBits(1, static_cast<uint16_t>(group * 8), 8, status8);
}

bool TcpPlcInstance::readS(int sDecimal, bool& state) {
	std::lock_guard<std::mutex> lock(mutex_);
	// S 区映射在线圈区(Coil), 用 FC1 读线圈而非 FC2 离散输入
	return connected_ && readBit(1, sAddress(sDecimal), state);
}

const std::string& TcpPlcInstance::getPlcId() const {
	return plcId_;
}

uint16_t TcpPlcInstance::xAddress(int xOctal) {
	return static_cast<uint16_t>((xOctal / 10) * 8 + (xOctal % 10));
}

uint16_t TcpPlcInstance::yAddress(int yOctal) {
	return static_cast<uint16_t>((yOctal / 10) * 8 + (yOctal % 10));
}

uint16_t TcpPlcInstance::mAddress(int mDecimal) {
	// 驿公里 FX5U Modbus 从站: M 区映射线圈区 0x2000(8192) 起, M0 -> 8192
	// 不加偏移会访问到映射区外的地址, PLC 回非法数据地址或读到错误数据
	return static_cast<uint16_t>(mDecimal + 8192);
}

uint16_t TcpPlcInstance::sAddress(int sDecimal) {
	// FX5 专用模式默认软元件分配: S0~S4095 映射线圈区 B000H(45056) 起
	return static_cast<uint16_t>(sDecimal + 45056);
}
