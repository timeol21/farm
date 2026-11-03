#pragma once

#include <cstdint>
#include <string>

class IPlcInstance {
public:
	virtual ~IPlcInstance() = default;

	virtual bool connect() = 0;
	virtual void disconnect() = 0;
	virtual bool isConnected() const = 0;

	virtual bool forceY(int yOctal, bool turnOn) = 0;
	virtual bool forceX(int xOctal, bool turnOn) = 0;
	virtual bool forceM(int mDecimal, bool turnOn) = 0;

	virtual bool readD(int dNumber, uint16_t& value) = 0;
	virtual bool readYBit(int yOctal, bool& state) = 0;
	virtual bool readM(int mDecimal, bool& state) = 0;
	virtual bool readXBit(int xOctal, bool& state) = 0;
	virtual bool readX(int group, int& status8) = 0;
	virtual bool readY(int group, int& status8) = 0;
	virtual bool readS(int sDecimal, bool& state) = 0;

	virtual const std::string& getPlcId() const = 0;
};
