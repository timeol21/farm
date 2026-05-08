#include <cstdint>              //测试可用
#include <vector>
#include <iostream>
#include <iomanip>  // 用于格式化输出十六进制

// Modbus CRC16 核心计算函数（支持任意长度数据）
uint16_t modbus_crc16(const unsigned char* data, int len)
{
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
        {
            if (crc & 0x0001)
            {
                crc >>= 1;
                crc ^= 0xA001;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return crc;
}

// 支持 vector 任意长度
uint16_t modbus_crc16(const std::vector<unsigned char>& bytes)
{
    return modbus_crc16(bytes.data(), bytes.size());
}

// ---------------------- 测试：任意长度 sendBuf ----------------------
int main()
{
    // 你可以随便改这里的内容 & 长度！！！
    // 示例 1：6 字节
    std::vector<unsigned char> sendBuf = {0x01, 0x05, 0x05, 0x00, 0xFF, 0x00};

    // 示例 2：8 字节（变长测试）
    // std::vector<unsigned char> sendBuf = {0x01, 0x03, 0x00, 0x01, 0x00, 0x02, 0x11, 0x22};

    // 示例 3：4 字节（变长测试）
    // std::vector<unsigned char> sendBuf = {0x01, 0x05, 0x00, 0x00};

    // 计算 CRC
    uint16_t crc = modbus_crc16(sendBuf);

    // 拆分 CRC 高低位（Modbus 协议要求：低字节在前，高字节在后）
    unsigned char crc_low  = crc & 0xFF;         // 低8位
    unsigned char crc_high = (crc >> 8) & 0xFF;  // 高8位

    // 输出结果
    std::cout << "数据长度: " << sendBuf.size() << " 字节" << std::endl;
    std::cout << "CRC16 结果: 0x" << std::hex << std::uppercase << (int)crc << std::endl;
    std::cout << "CRC 低8位: 0x" << std::setw(2) << std::setfill('0') << (int)crc_low  << std::endl;
    std::cout << "CRC 高8位: 0x" << std::setw(2) << std::setfill('0') << (int)crc_high << std::endl;

    // 最终可直接拼接发送的完整指令（数据 + 低字节CRC + 高字节CRC）
    std::cout << "\n完整 Modbus 指令(可直接发送): ";
    for (auto b : sendBuf) printf("%02X ", b);
    printf("%02X %02X\n", crc_low, crc_high);

    return 0;
}