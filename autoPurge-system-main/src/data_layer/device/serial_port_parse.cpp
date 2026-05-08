#include "data_layer/device/serial_port_parse.h"




// [0] slave addr
// [1] function code (0x03)
// [2] byte count
// [3..] data
// [last-2][last-1] CRC

SensorData ModbusParser::parseTempHum(const std::vector<uint8_t>& resp){
    SensorData data{};

    // ===== 1. 最小长度校验 =====
    // [addr][func][len][data...][crc_lo][crc_hi]
    if (resp.size() < 7) {
        return data;
    }

    // ===== 2. 功能码校验 =====
    if (resp[1] != 0x03) {
        return data;
    }

    // ===== 3. 字节数校验 =====
    uint8_t byteCount = resp[2];

    // 温湿度一般是 2个寄存器 = 4字节
    if (byteCount != 4 || resp.size() < 3 + byteCount + 2) {
        return data;
    }

    // ===== 4. 数据解析（大端）=====
    uint16_t rawHumi = (resp[3] << 8) | resp[4];
    uint16_t rawTemp = (resp[5] << 8) | resp[6];

    data.humidity    = rawHumi / 10.0f;
    data.temperature = rawTemp / 10.0f;
    data.valid = true;
    return data;
}
