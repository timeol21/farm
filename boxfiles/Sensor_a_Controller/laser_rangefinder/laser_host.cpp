#include "laser_host.h"             //激光测距仪的实现代码

LaserHost::LaserHost(const char* port) : m_port(port), m_fd(-1) {}

// bool LaserHost::grantPermission() {         //串口赋权
//     std::cout << "=== Grant permission ===" << std::endl;
//     std::string cmd = "chmod 666 " + m_port;
//     return system(cmd.c_str()) != -1;
// }

bool LaserHost::grantPermission() {
    std::cout << "=== Step 1: Grant serial port permission ===" << std::endl;
    std::string cmd = "chmod 666 " + m_port;
    int ret = system(cmd.c_str());

    if (ret == -1) {
        std::cerr << "❌ Error: Failed to execute chmod command" << std::endl;
        return false;
    } else if (WEXITSTATUS(ret) != 0) {
        std::cerr << "❌ Error: chmod failed, please run with sudo" << std::endl;
        return false;
    }

    std::cout << "✅ Success: Port permission granted → " << m_port << std::endl;
    return true;
}

int LaserHost::configSerial(int fd) {
    std::cout << "\n=== Step 3: Configure serial (115200 8N1) ===" << std::endl;
    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(fd, &tty) != 0) {
        std::cerr << "❌ Error: Get serial attribute failed" << std::endl;
        return -1;
    }

    cfsetospeed(&tty, m_baudrate);
    cfsetispeed(&tty, m_baudrate);
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= CREAD | CLOCAL;

    tty.c_lflag = 0;
    tty.c_oflag = 0;
    tty.c_iflag = 0;
    tty.c_cc[VTIME] = 0;
    tty.c_cc[VMIN] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        std::cerr << "❌ Error: Set serial attribute failed" << std::endl;
        return -2;
    }

    tcflush(fd, TCIOFLUSH);
    std::cout << "✅ Success: Serial configured 115200 8N1" << std::endl;
    return 0;
}

// int LaserHost::openSerial() {             //打开串口
//     int fd = open(m_port.c_str(), O_RDWR | O_NOCTTY);
//     if (fd < 0) return -1;
//     if (configSerial(fd) != 0) {
//         close(fd);
//         return -2;
//     }
//     return fd;
// }

int LaserHost::openSerial() {
    std::cout << "\n=== Step 2: Open serial port ===" << std::endl;
    int fd = open(m_port.c_str(), O_RDWR | O_NOCTTY);
    if (fd < 0) {
        std::cerr << "❌ Error: Failed to open " << m_port << std::endl;
        return -1;
    }
    std::cout << "✅ Success: Serial port opened → " << m_port << std::endl;

    if (configSerial(fd) != 0) {
        close(fd);
        return -2;
    }
    return fd;
}

// bool LaserHost::init() {     //激光测距仪回路初始化
//     if (!grantPermission()) return false;
//     m_fd = openSerial();
//     if (m_fd < 0) return false;
//     std::this_thread::sleep_for(std::chrono::milliseconds(150));
//     sendBaudMatch();           //发送自动校验波特率的报文
//     return true;
// }

bool LaserHost::init() {
    std::cout << "================================================" << std::endl;
    std::cout << "          Laser Host Initializing               " << std::endl;
    std::cout << "================================================" << std::endl;

    if (!grantPermission()) {
        std::cerr << "\n❌ Init failed: grant permission error" << std::endl;
        return false;
    }

    m_fd = openSerial();
    if (m_fd < 0) {
        std::cerr << "\n❌ Init failed: open serial error" << std::endl;
        return false;
    }

    std::cout << "\n=== Step 4: Sensor power on delay (150ms) ===" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    sendBaudMatch();

    std::cout << "\n================================================" << std::endl;
    std::cout << "✅ Laser Host Init ALL SUCCESS!!" << std::endl;
    std::cout << "================================================" << std::endl;
    return true;
}

// void LaserHost::deinit() {     
//     if (m_fd >= 0) close(m_fd);
// }

void LaserHost::deinit() {
    std::cout << "\n================================================" << std::endl;
    std::cout << "=== Step Final: Deinitialize Laser Host ===" << std::endl;

    if (m_fd >= 0) {
        close(m_fd);
        m_fd = -1;
        std::cout << "✅ Success: Serial port closed safely" << std::endl;
    } else {
        std::cout << "⚠️ Warning: Serial port was already closed" << std::endl;
    }

    std::cout << "✅ Deinit complete → Program exit safely" << std::endl;
    std::cout << "================================================" << std::endl;
}

// int LaserHost::sendPacket(const uint8_t* pkt, int len, const char* name) {
//     std::cout << "\n[" << name << "] (" << len << " bytes): ";
//     for (int i=0; i<len; i++) {
//         std::cout << std::hex << std::uppercase << std::setw(2) << (int)pkt[i] << " ";
//     }
//     std::cout << std::dec << std::endl;

//     int w = write(m_fd, pkt, len);
//     if (w != len) {
//         std::cerr << "Send fail\n";
//         return -1;
//     }
//     return 0;
// }

int LaserHost::sendPacket(const uint8_t* pkt, int len, const char* name) {
    std::cout << "\n[" << name << "] (" << len << " bytes): ";
    for (int i = 0; i < len; i++) {
        std::cout << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << (int)pkt[i] << " ";
    }
    std::cout << std::dec << std::endl;

    int w = write(m_fd, pkt, len);
    if (w != len) {
        std::cerr << "❌ Error: Send failed (" << w << "/" << len << " bytes)" << std::endl;
        return -1;
    }

    // ✅ 发送成功提示
    std::cout << "✅ Success: Packet sent successfully" << std::endl;
    return 0;
}

// ====================== 波特率匹配 ======================
// int LaserHost::sendBaudMatch() {
//     uint8_t pkt[] = {0x55};
//     return sendPacket(pkt, 1, "Baud Match");
// }

int LaserHost::sendBaudMatch() {
    std::cout << "\n=== Step 5: Send baud rate match (0x55) ===" << std::endl;
    uint8_t pkt[] = {0x55};
    int ret = sendPacket(pkt, 1, "Baud Match");
    if (ret == 0) {
        std::cout << "✅ Success: Baud match packet sent" << std::endl;
    } else {
        std::cerr << "❌ Error: Baud match send failed" << std::endl;
    }
    return ret;
}

// ====================== 停止连续测量 ======================
// int LaserHost::stopContinuousMeasure() {
//     uint8_t pkt[] = {0X58};
//     return sendPacket(pkt, 1, "STOP CONTINUOUS (X)");
// }

int LaserHost::stopContinuousMeasure() {
    std::cout << "\n=== Step: Stop Continuous Measurement (0x58) ===" << std::endl;
    uint8_t pkt[] = {0X58};
    int ret = sendPacket(pkt, 1, "STOP CONTINUOUS (X)");

    if (ret == 0) {
        std::cout << "✅ Success: Stop continuous measure command sent" << std::endl;
    } else {
        std::cerr << "❌ Error: Failed to send stop command" << std::endl;
    }
    return ret;
}

// ====================== 一次快速测量 ======================
int LaserHost::singleMeasureFast(uint8_t addr) {
    uint8_t pkt[] = {0xAA, addr, 0x00, 0x20, 0x00, 0x01, 0x00, 0x02, 0x23 + addr};
    return sendPacket(pkt, 9, "Single Measure FAST");
}

// ====================== 一次慢速测量 ======================
int LaserHost::singleMeasureSlow(uint8_t addr) {
    uint8_t pkt[] = {0xAA, addr, 0x00, 0x20, 0x00, 0x01, 0x00, 0x01, 0x22 + addr};
    return sendPacket(pkt, 9, "Single Measure SLOW");
}

// ====================== 一次自动测量 ======================
int LaserHost::singleMeasureAuto(uint8_t addr) {
    uint8_t pkt[] = {0xAA, addr, 0x00, 0x20, 0x00, 0x01, 0x00, 0x00, 0x21 + addr};
    return sendPacket(pkt, 9, "Single Measure AUTO");
}

// ====================== 自动连续 ======================
int LaserHost::continuousMeasureFast(uint8_t addr) {
    uint8_t pkt[] = {0xAA, addr, 0x00, 0x20, 0x00, 0x01, 0x00, 0x04, 0x25 + addr};
    return sendPacket(pkt, 9, "Continuous FAST");
}

// ====================== 慢速连续 ======================
int LaserHost::continuousMeasureSlow(uint8_t addr) {
    uint8_t pkt[] = {0xAA, addr, 0x00, 0x20, 0x00, 0x01, 0x00, 0x05, 0x26 + addr};
    return sendPacket(pkt, 9, "Continuous SLOW");
}

// ====================== 快速连续 ======================
int LaserHost::continuousMeasureAuto(uint8_t addr) {
    uint8_t pkt[] = {0xAA, addr, 0x00, 0x20, 0x00, 0x01, 0x00, 0x06, 0x27 + addr};
    return sendPacket(pkt, 9, "Continuous AUTO");
}

// ====================== 设置从机地址 ======================
int LaserHost::setSlaveAddress(uint8_t old_addr, uint8_t new_addr) {
    if (new_addr == 0x7F) {
        std::cerr << "❌ Error: CANNOT set address to 0x7F (invalid address)" << std::endl;
        return -1;
    }
    uint8_t pkt[] = {0xAA, old_addr, 0x00, 0x10, 0x00, 0x01, 0x00, new_addr, 0x11 + old_addr + new_addr};
    return sendPacket(pkt, 9, "Set Slave Address");
}

// ====================== 设置测量偏移 ======================
int LaserHost::setMeasureOffset(uint8_t addr, int offset_mm) {
    uint8_t pkt[] = {
        0xAA,           // 字节0: Head (0xAA，不参与校验)
        addr,           // 字节1: RW/Address (从机地址)
        0x00,           // 字节2: Register高 (0x0012的高字节)
        0x12,           // 字节3: Register低 (0x0012的低字节)
        0x00,           // 字节4: Payload count高 (0x0001的高字节)
        0x01,           // 字节5: Payload count低 (0x0001的低字节，固定1)
        (uint8_t)(offset_mm >> 8),  // 字节6: Payload高(ZZ) → 16位偏移的高字节
        (uint8_t)offset_mm,          // 字节7: Payload低(YY) → 16位偏移的低字节
        0x00            // 字节8: Checksum (占位，后续计算)
    };

    // 3. 按文档规则计算校验和：0xAA不参与，从字节1加到字节7
    uint8_t sum = 0;
    for (int i = 1; i < 8; i++) {  // i从1开始，跳过0xAA
        sum += pkt[i];
    }
    // uint8_t自动忽略溢出，完全符合文档要求
    pkt[8] = sum;

    return sendPacket(pkt, 9, "Set Measure Offset");
}

// ====================== 读取指定模块状态 ======================
int LaserHost::broadcastAllMeasure() {
    uint8_t pkt[] = {0xAA, 0x7F, 0x00, 0x20, 0x00, 0x01, 0x00, 0x00, 0xA0};
    return sendPacket(pkt, 9, "Get Module Status");
}

// ====================== 控制指定模块激光开关 ======================
int LaserHost::laserOn(uint8_t addr) {
    uint8_t pkt[] = {0xAA, addr, 0x01, 0xBE, 0x00, 0x01, 0x00, 0x01, 0xC1 + addr};
    return sendPacket(pkt, 9, "Laser ON");
}

int LaserHost::laserOff(uint8_t addr) {
    uint8_t pkt[] = {0xAA, addr, 0x01, 0xBE, 0x00, 0x01, 0x00, 0x00, 0xC0 + addr};
    return sendPacket(pkt, 9, "Laser OFF");
}

void LaserHost::allLaserOn(const std::vector<uint8_t>& addrs) {
    for (auto a : addrs) { laserOn(a); std::this_thread::sleep_for(std::chrono::milliseconds(100)); }
}

void LaserHost::allLaserOff(const std::vector<uint8_t>& addrs) {
    for (auto a : addrs) { laserOff(a); std::this_thread::sleep_for(std::chrono::milliseconds(100)); }
}

// ====================== 读取响应（完全复用你原来的） ======================
int LaserHost::readResponse(uint8_t* buffer, int timeout_ms) {
    memset(buffer, 0, 512);
    int total = 0;
    auto start = std::chrono::steady_clock::now();

    while (true) {
        int r = read(m_fd, buffer + total, 512 - total);
        if (r > 0) total += r;

        if (total >=13 && buffer[0]==0xAA && buffer[3]==0x22 && buffer[5]==0x03) break;

        auto elap = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elap > timeout_ms) break;

        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }

    if (total <=0) { std::cerr << "Read timeout\n"; return -1; }

    std::cout << "[Recv] (" << total << " bytes): ";
    for(int i=0; i<total; i++) std::cout << std::hex << std::uppercase << std::setw(2) << (int)buffer[i] << " ";
    std::cout << std::dec << std::endl;
    return total;
}

// ====================== 解析距离（完全你原来的） ======================
bool LaserHost::parseDistance(const uint8_t* response, int len, int& distance_mm) {
    if (len <13 || response[0]!=0xAA || response[3]!=0x22) {
        std::cerr << "Invalid frame\n";
        return false;
    }

    distance_mm = (response[6]<<24) | (response[7]<<16) | (response[8]<<8) | response[9];
    if (distance_mm <30 || distance_mm>20000) {
        std::cerr << "Range error\n";
        return false;
    }

    uint8_t calc = 0;
    for(int i=1; i<len-1; i++) calc += response[i];

    std::cout << "✅ Distance: " << distance_mm << "mm | calc=" << (int)calc
              << " recv=" << (int)response[len-1] << std::endl;
    return true;
}