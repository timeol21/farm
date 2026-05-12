#include "data_layer/fx_plc/fx_plc_instance.h"
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <cstdio>
#include <iostream>
#include "common/log/log_manager.h"


FxPlcInstance::FxPlcInstance(const FxPlcDevice& device)
    : device_(device), serialFd_(-1), connected_(false) {}

FxPlcInstance::~FxPlcInstance() {
    disconnect();
}

// 移植自用户 openSerial 函数
int FxPlcInstance::openSerial(const char* dev, int baud, int dataBits, bool parityEven, int stopBits) {
    int fd = open(dev, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0) {
        perror(dev);
        return -1;
    }

    struct termios options;
    tcgetattr(fd, &options);

    // 转换波特率
    speed_t speed;
    switch (baud) {
        case 9600: speed = B9600; break;
        case 19200: speed = B19200; break;
        case 38400: speed = B38400; break;
        case 115200: speed = B115200; break;
        default: speed = B9600;
    }
    cfsetispeed(&options, speed);
    cfsetospeed(&options, speed);

    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~CSIZE;

    switch (dataBits) {
        case 5: options.c_cflag |= CS5; break;
        case 6: options.c_cflag |= CS6; break;
        case 7: options.c_cflag |= CS7; break;
        case 8: options.c_cflag |= CS8; break;
        default: options.c_cflag |= CS8; break;
    }

    if (parityEven) {
        options.c_cflag |= PARENB;
        options.c_cflag &= ~PARODD; // 偶校验
    } else {
        options.c_cflag &= ~PARENB;
    }

    if (stopBits == 2) {
        options.c_cflag |= CSTOPB;
    } else {
        options.c_cflag &= ~CSTOPB;
    }

    options.c_cflag &= ~CRTSCTS;
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    options.c_iflag |= IGNBRK | IGNPAR;
    options.c_oflag &= ~OPOST;

    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 10;   // 1秒超时

    tcsetattr(fd, TCSANOW, &options);
    return fd;
}

bool FxPlcInstance::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connected_) {
        LOG_INFO("FX PLC [" + device_.getPlcId() + "] 连接成功");
        std::cout<<"PLC"<<device_.getPlcId()+"已连接";
        return true;
    }

    serialFd_ = openSerial(device_.getSerialPort().c_str(),
                           device_.getBaudRate(),
                           device_.getDataBits(),
                           device_.getParityEven(),
                           device_.getStopBits());
    if (serialFd_ < 0) {
        connected_ = false;
        LOG_ERROR("FX PLC [" + device_.getPlcId() + "] 连接失败: " + strerror(errno));
        std::cerr << "[PLC] " << device_.getPlcId() << " 连接失败：" << strerror(errno) << std::endl;
        return false;
    }
    connected_ = true;
    return true;
}

void FxPlcInstance::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (serialFd_ >= 0) {
        close(serialFd_);
        serialFd_ = -1;
    }
    connected_ = false;
}

bool FxPlcInstance::isConnected() const {
    return connected_;
}


void FxPlcInstance::calculateAndAppendSum(unsigned char* buf, int startIdx, int len) {
    unsigned char sum = 0;
    for (int i = 0; i < len; i++) {
        sum += buf[startIdx + i];
    }
    char sumStr[3];
    sprintf(sumStr, "%02X", sum);
    buf[startIdx + len] = sumStr[0];
    buf[startIdx + len + 1] = sumStr[1];
}

bool FxPlcInstance::sendAndReceive(const unsigned char* sendBuf, int sendLen,
                                   unsigned char* recvBuf, int* recvLen) {
    tcflush(serialFd_, TCIOFLUSH);

    int n = write(serialFd_, sendBuf, sendLen);
    if (n != sendLen) {
        std::cerr << "发送失败" << std::endl;
        return false;
    }
    usleep(200000); // 等待PLC响应

    unsigned char firstByte;
    n = read(serialFd_, &firstByte, 1);
    if (n != 1) {
        std::cerr << "读取首字节失败" << std::endl;
        return false;
    }

    if (firstByte == 0x06 || firstByte == 0x15) {
        recvBuf[0] = firstByte;
        *recvLen = 1;
        return true;
    } else if (firstByte == 0x02) {
        recvBuf[0] = firstByte;
        int idx = 1;
        while (true) {
            unsigned char byte;
            n = read(serialFd_, &byte, 1);
            if (n != 1) {
                std::cerr << "读取数据不完整" << std::endl;
                return false;
            }
            recvBuf[idx++] = byte;
            if (byte == 0x03) {
                n = read(serialFd_, &recvBuf[idx], 2);
                if (n != 2) {
                    std::cerr << "读取校验和失败" << std::endl;
                    return false;
                }
                idx += 2;
                break;
            }
            if (idx > 256) {
                std::cerr << "响应过长" << std::endl;
                return false;
            }
        }
        *recvLen = idx;
        return true;
    } else {
        std::cerr << "未知的响应首字节: 0x" << std::hex << (int)firstByte << std::dec << std::endl;
        return false;
    }
}

// 地址转换函数
int FxPlcInstance::yOctalToAddr(int yOctal) {
    int yDecimal = (yOctal / 10) * 8 + (yOctal % 10);
    return yDecimal + 0x500;
}

int FxPlcInstance::mDecimalToForceAddr(int mDecimal) {
    return mDecimal + 0x1000;
}

int FxPlcInstance::mDecimalToReadAddr(int mDecimal) {
    return 0x0100 + mDecimal;
}
//读取s点
bool FxPlcInstance::readS(int sDecimal, bool& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_) return false;

    int addr = sDecimal; // S0 address = 0x0000
    char addrStr[5];
    sprintf(addrStr, "%04X", addr);

    unsigned char cmd[11] = {
        0x02, 0x30,
        addrStr[0], addrStr[1], addrStr[2], addrStr[3],
        '0', '1',
        0x03, 0x00, 0x00
    };
    calculateAndAppendSum(cmd, 1, 8);

    unsigned char recvBuf[256];
    int recvLen = 0;
    if (!sendAndReceive(cmd, 11, recvBuf, &recvLen)) {
        return false;
    }

    if (recvBuf[0] == 0x02) {
        char dataHex[3] = {static_cast<char>(recvBuf[1]), static_cast<char>(recvBuf[2]), '\0'};
        int data = strtol(dataHex, NULL, 16);
        int bitOffset = sDecimal % 8;
        state = (data >> bitOffset) & 1;
        return true;
    } else {
        std::cerr << "读取S点失败，首字节不是STX" << std::endl;
        return false;
    }
}

// 控制 Y 点
bool FxPlcInstance::forceY(int yOctal, bool turnOn) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_) return false;

    int addr = yOctalToAddr(yOctal);
    char addrLowStr[3], addrHighStr[3];
    sprintf(addrLowStr, "%02X", addr & 0xFF);
    sprintf(addrHighStr, "%02X", (addr >> 8) & 0xFF);

    unsigned char cmd[11] = {0x02};
    cmd[1] = turnOn ? 0x37 : 0x38;   // 37=置位, 38=复位
    cmd[2] = addrLowStr[0];
    cmd[3] = addrLowStr[1];
    cmd[4] = addrHighStr[0];
    cmd[5] = addrHighStr[1];
    cmd[6] = 0x03;
    cmd[7] = 0x00;
    cmd[8] = 0x00;

    calculateAndAppendSum(cmd, 1, 6);

    unsigned char recvBuf[10];
    int recvLen = 0;
    if (sendAndReceive(cmd, 9, recvBuf, &recvLen)) {
        if (recvLen == 1 && recvBuf[0] == 0x06) {
            std::cout << "成功" << (turnOn ? "置位" : "复位") << " Y" << std::oct << yOctal << std::dec << std::endl;
            return true;
        } else if (recvLen == 1 && recvBuf[0] == 0x15) {
            std::cerr << "PLC拒绝操作 (返回NAK)，请检查Y地址或PLC状态。" << std::endl;
        } else {
            std::cerr << "收到未知响应。" << std::endl;
        }
    }
    return false;
}

// 控制 M 点
bool FxPlcInstance::forceM(int mDecimal, bool turnOn) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_) return false;

    int addr = 0x0800 + mDecimal;   // M 强制地址基址 + 十进制编号（对齐你新代码的地址计算）

    char addrLowStr[3];
    char addrHighStr[3];
    sprintf(addrLowStr, "%02X", addr & 0xFF);
    sprintf(addrHighStr, "%02X", (addr >> 8) & 0xFF);

    unsigned char cmd[11] = {0x02};
    cmd[1] = turnOn ? 0x37 : 0x38;
    cmd[2] = addrLowStr[0];
    cmd[3] = addrLowStr[1];
    cmd[4] = addrHighStr[0];
    cmd[5] = addrHighStr[1];
    cmd[6] = 0x03;   // ETX
    cmd[7] = 0x00;   // 校验和占位
    cmd[8] = 0x00;

    calculateAndAppendSum(cmd, 1, 6);   // 从命令码到ETX共6字节

    unsigned char recvBuf[10];
    int recvLen = 0;
    // 修正：去掉 fd，直接调用成员函数 sendAndReceive
    if (sendAndReceive(cmd, 9, recvBuf, &recvLen)) {
        if (recvLen == 1 && recvBuf[0] == 0x06) {
            std::cout << "成功" << (turnOn ? "置位" : "复位")
                      << " M" << mDecimal << std::endl;
            return true;
        } else if (recvLen == 1 && recvBuf[0] == 0x15) {
            std::cerr << "PLC拒绝操作M点（返回NAK）" << std::endl;
        } else {
            std::cerr << "收到未知响应" << std::endl;
        }
    }
    return false;
}

// 读取 D 寄存器
bool FxPlcInstance::readD(int dNumber, uint16_t& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_) return false;

    int addr = 0x1000 + dNumber;
    char addrStr[5];
    sprintf(addrStr, "%04X", addr);

    unsigned char cmd[13] = {
        0x02, 0x30,
        addrStr[0], addrStr[1], addrStr[2], addrStr[3],
        '0', '2',
        0x03, 0x00, 0x00
    };
    calculateAndAppendSum(cmd, 1, 8);

    unsigned char recvBuf[256];
    int recvLen = 0;
    if (!sendAndReceive(cmd, 11, recvBuf, &recvLen)) {
        std::cerr << "发送/接收失败" << std::endl;
        return false;
    }

    if (recvLen < 5 || recvBuf[0] != 0x02) {
        std::cerr << "无效响应" << std::endl;
        return false;
    }

    char dataHex[5] = {static_cast<char>(recvBuf[1]), static_cast<char>(recvBuf[2]),
                       static_cast<char>(recvBuf[3]), static_cast<char>(recvBuf[4]), '\0'};
    char* endptr;
    long val = strtol(dataHex, &endptr, 16);
    if (endptr != dataHex + 4) {
        std::cerr << "D值转换失败" << std::endl;
        return false;
    }
    value = static_cast<uint16_t>(val);
    return true;
}

// 读取单个 Y 点
bool FxPlcInstance::readYBit(int yOctal, bool& state) {
    int group = yOctal / 10;
    int bitOffset = yOctal % 10;
    if (group < 0 || group > 9) {
        std::cerr << "Y地址超出范围（0-377八进制）" << std::endl;
        return false;
    }

    int status8 = 0;
    if (!readY(group, status8)) return false;
    state = (status8 >> bitOffset) & 1;
    return true;
}

// 读取一组 Y 点
bool FxPlcInstance::readY(int group, int& status8) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_) return false;

    if (group < 0 || group > 9) {
        std::cerr << "错误：组号必须为0~9" << std::endl;
        return false;
    }

    unsigned char cmd[11] = {
        0x02, 0x30,
        '0', '0',
        'A',
        static_cast<unsigned char>('0' + group),
        '0', '1',
        0x03, 0x00, 0x00
    };
    calculateAndAppendSum(cmd, 1, 8);

    unsigned char recvBuf[256];
    int recvLen = 0;
    if (!sendAndReceive(cmd, 11, recvBuf, &recvLen)) {
        std::cerr << "发送/接收失败" << std::endl;
        return false;
    }

    if (recvLen < 3 || recvBuf[0] != 0x02) {
        std::cerr << "无效响应" << std::endl;
        return false;
    }
    char dataHex[3] = {static_cast<char>(recvBuf[1]), static_cast<char>(recvBuf[2]), '\0'};
    char* endptr;
    long val = strtol(dataHex, &endptr, 16);
    if (endptr == dataHex) {
        std::cerr << "状态值转换失败" << std::endl;
        return false;
    }
    status8 = static_cast<int>(val);
    return true;
}

// 读取单个 M 点
bool FxPlcInstance::readM(int mDecimal, bool& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_) return false;

    int block = mDecimal / 256;               // M0~255: block0, M256~511: block1
    int offsetInBlock = mDecimal % 256;       // 块内偏移（0~255）
    int byteIndex = offsetInBlock / 8;        // 该M点所在的字节索引（0~31）
    int addr = 0x0800 + block * 0x0100 + byteIndex; // 协议地址

    char addrStr[5];
    sprintf(addrStr, "%04X", addr);           // 高位在前

    unsigned char cmd[11] = {
        0x02, 0x30,                           // STX + 读取命令
        addrStr[0], addrStr[1], addrStr[2], addrStr[3], // 4字节地址
        '0', '1',                             // 读取1个字节
        0x03,                                 // ETX
        0x00, 0x00                            // 校验和占位
    };
    calculateAndAppendSum(cmd, 1, 8);         // 从命令码到ETX共8字节


    unsigned char recvBuf[256];
    int recvLen = 0;
    if (!sendAndReceive(cmd, 11, recvBuf, &recvLen)) {
        std::cerr << "发送/接收失败" << std::endl;
        return false;
    }

    for (int i = 0; i < recvLen; ++i) {
        printf("%02X ", recvBuf[i]);
    }

    if (recvLen < 5 || recvBuf[0] != 0x02) {
        std::cerr << "无效响应或首字节不是 STX" << std::endl;
        return false;
    }

    char dataHex[3] = {static_cast<char>(recvBuf[1]), static_cast<char>(recvBuf[2]), '\0'};
    int data = strtol(dataHex, NULL, 16);     
    int bitOffset = mDecimal % 8;             
    state = (data >> bitOffset) & 1;

    return true;
}

const std::string& FxPlcInstance::getPlcId() const {
    return device_.getPlcId();
}

//读取单个y点

bool FxPlcInstance::readXBit(int xOctal, bool& state) {
    int group = xOctal / 10;          // 组号，例如 X36 -> group=3
    int bitOffset = xOctal % 10;      // 位偏移，X36 -> bit=6

    if (group < 0 || group > 9) {
        std::cerr << "X地址超出范围（0-77八进制）" << std::endl;
        return false;
    }

    int status8 = 0;
    if (!readX(group, status8)) return false;
    state = (status8 >> bitOffset) & 1;
    return true;
}

bool FxPlcInstance::readX(int group, int& status8) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_) return false;

    if (group < 0 || group > 7) {   // 
        std::cerr << "错误：X 组号必须为0~7" << std::endl;
        return false;
    }

    int addr = 0x0080 + group;
    char addrStr[5];
    sprintf(addrStr, "%04X", addr);  // 高位在前

    unsigned char cmd[11] = {
        0x02, 0x30,
        addrStr[0], addrStr[1], addrStr[2], addrStr[3],
        '0', '1',
        0x03, 0x00, 0x00
    };
    calculateAndAppendSum(cmd, 1, 8);

    unsigned char recvBuf[256];
    int recvLen = 0;
    if (!sendAndReceive(cmd, 11, recvBuf, &recvLen)) {
        std::cerr << "发送/接收失败" << std::endl;
        return false;
    }

    if (recvLen < 3 || recvBuf[0] != 0x02) {
        std::cerr << "无效响应" << std::endl;
        return false;
    }
    char dataHex[3] = {static_cast<char>(recvBuf[1]), static_cast<char>(recvBuf[2]), '\0'};
    char* endptr;
    long val = strtol(dataHex, &endptr, 16);
    if (endptr == dataHex) {
        std::cerr << "状态值转换失败" << std::endl;
        return false;
    }
    status8 = static_cast<int>(val);
    return true;
}