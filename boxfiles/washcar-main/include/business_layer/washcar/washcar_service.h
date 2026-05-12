#pragma once
#include "business_layer/device/device_service.h"
#include "business_layer/lobby/lobby_object.h"
#include <string>
#include <nlohmann/json.hpp>

class IWashCarSercvice {
public:
	virtual ~IWashCarSercvice() = default;

	// 启动洗车
	virtual WashCarOperationResult startWashCar(const WashCarOperation& operation) = 0;

	// 停止洗车
	virtual WashCarOperationResult stopWashCar(const WashCarOperation& operation) = 0;

	// 复位洗车系统
	virtual WashCarOperationResult resetWashCar(const WashCarOperation& operation) = 0;

	// 获取洗车状态
	virtual WashCarOperationResult getWashCarStatus(const WashCarOperation& operation) = 0;

	// 是否正在洗车中
	virtual bool isWashing() const = 0;
};

class WashCarService : public IWashCarSercvice {
public:
	WashCarService(IDeviceService& deviceService);
	~WashCarService();

	// 实现接口：启动洗车
	WashCarOperationResult startWashCar(const WashCarOperation& operation) override;

	// 实现接口：停止洗车
	WashCarOperationResult stopWashCar(const WashCarOperation& operation) override;

	// 实现接口：复位洗车系统
	WashCarOperationResult resetWashCar(const WashCarOperation& operation) override;

	// 实现接口：获取洗车状态
	WashCarOperationResult getWashCarStatus(const WashCarOperation& operation) override;

	// 实现接口：是否正在洗车中
	bool isWashing() const override;

private:
	IDeviceService& deviceService;

	// true = 处于Web维护模式，屏蔽云端控制
	bool m_isWebMaintainMode = false;

	WashCarOperationResult m_currentStatus;

private:
	//检查故障 / 报警点位全为0
	bool checkCurrentAlarms();

};