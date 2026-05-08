#include <cstdint>  // 必须包含：用于uint16_t等类型
#include <vector>   // 可选：若需要支持vector入参
#include <iostream> // 可选：仅用于示例测试

// MODBUS RTU 标准CRC16计算函数（核心独立功能）
// 参数说明：
//   data: 待计算CRC的字节数组（unsigned char* 类型，通用指针）
//   len:  字节数组的长度
// 返回值：计算得到的16位CRC值（uint16_t）
uint16_t modbus_crc16(const unsigned char* data, int len)
{
    uint16_t crc = 0xFFFF;  // CRC初始值固定为0xFFFF
    for (int i = 0; i < len; i++)
    {
        crc ^= data[i];     // 与当前字节异或
        // 逐位计算
        for (int j = 0; j < 8; j++)
        {
            if (crc & 0x0001)  // 最低位为1
            {
                crc >>= 1;     // 右移1位
                crc ^= 0xA001; // 与多项式0xA001异或（MODBUS标准）
            }
            else
            {
                crc >>= 1;     // 最低位为0，仅右移
            }
        }
    }
    return crc;
}

// 【可选】适配vector的重载函数（更易用，无需手动传长度）
// 参数说明：
//   bytes: 待计算CRC的vector<unsigned char> 字节数组
// 返回值：计算得到的16位CRC值（uint16_t）
uint16_t modbus_crc16(const std::vector<unsigned char>& bytes)
{
    return modbus_crc16(bytes.data(), bytes.size());
}

// -------------------------- 示例：如何在你的程序中使用 --------------------------
int main()
{
    // 示例1：计算普通字节数组的CRC（如PLC指令：01 01 00 00 00 01）
    unsigned char plc_cmd[] = {0x01, 0x01, 0x00, 0x00, 0x00, 0x01};
    uint16_t crc1 = modbus_crc16(plc_cmd, 6);
    std::cout << "示例1 CRC值（十六进制）：" 
              << std::hex << std::uppercase << (crc1 >> 8) // 高8位
              << " " << (crc1 & 0xFF) << std::endl;       // 低8位

    // 示例2：计算vector字节数组的CRC（和你原有代码兼容）
    std::vector<unsigned char> sendBuf = {0x02, 0x01, 0x00, 0x00, 0x00, 0x01};
    uint16_t crc2 = modbus_crc16(sendBuf);
    std::cout << "示例2 CRC值（十六进制）：" 
              << std::hex << std::uppercase << (crc2 >> 8) 
              << " " << (crc2 & 0xFF) << std::endl;

    // 示例3：将CRC拆分为高低8位（用于拼接指令）
    unsigned char crc_high = (crc1 >> 8) & 0xFF; // CRC高8位
    unsigned char crc_low = crc1 & 0xFF;         // CRC低8位
    std::cout << "示例1 CRC拆分：高8位=0x" << std::hex << (int)crc_high 
              << " 低8位=0x" << (int)crc_low << std::endl;

    return 0;
}





#include <cstdint>      //测试可用
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
    std::vector<unsigned char> sendBuf = {0x02, 0x01, 0x00, 0x00, 0x00, 0x01};

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