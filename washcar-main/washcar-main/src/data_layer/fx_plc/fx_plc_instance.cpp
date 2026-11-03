#include "data_layer/fx_plc/fx_plc_instance.h"
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <cstdio>
#include <iostream>
#include "common/log/log_manager.h"
#include "common/log/trace_context.h"   // [TEMP_DIAG] 阻塞点定位 trace


namespace {
// 串口一收一发的等待时间。
// 9600 波特(现场这台 PLC)下就是原来写死的 10ms —— 现场行为逐位不变。
// 波特率提高 N 倍 ⇒ 等待缩 1/N, 与真实串口"波特率越高传得越快"一致。
// 本地 PTY 模拟器没有真实波特率, 把配置里的 baudRate 调高就能加快验证,
// 产品这边不需要知道自己被模拟。
int serialResponseDelayUs(int baudRate) {
    if (baudRate <= 0) return 10000;              // 没配波特率 ⇒ 沿用生产值
    const long long delay = 10000LL * 9600 / baudRate;
    if (delay <= 0) return 10000;
    return static_cast<int>(delay);
}
}  // namespace

FxPlcInstance::FxPlcInstance(const FxPlcDevice& device)
    : device_(device), serialFd_(-1), connected_(false) {}

FxPlcInstance::~FxPlcInstance() {
    disconnect();
}

int FxPlcInstance::openSerial(const char* dev, int baud, int dataBits, bool parityEven, int stopBits) {
    //修改为阻塞模式，依靠VTIME来控制超时
    int fd = open(dev, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror(dev);
        return -1;
    }

    struct termios options;
    if (tcgetattr(fd, &options) != 0) {
        perror("tcgetattr failed");
        close(fd);
        return -1;
    }
    cfmakeraw(&options);

    // 转换波特率
    speed_t speed;
    switch (baud) {
        case 9600: speed = B9600; break;
        case 19200: speed = B19200; break;
        case 38400: speed = B38400; break;
        case 57600: speed = B57600; break;
        case 115200: speed = B115200; break;
        default: speed = B9600;
    }
    cfsetispeed(&options, speed);
    cfsetospeed(&options, speed);

    // 启用接收和本地模式
    options.c_cflag |= (CLOCAL | CREAD);

    // 设置数据位
    options.c_cflag &= ~CSIZE;
    switch (dataBits) {
        case 5: options.c_cflag |= CS5; break;
        case 6: options.c_cflag |= CS6; break;
        case 7: options.c_cflag |= CS7; break;
        case 8: 
        default: options.c_cflag |= CS8; break;
    }

    // 设置校验位
    if (parityEven) {
        options.c_cflag |= PARENB;
        options.c_cflag &= ~PARODD; // 偶校验
    } else {
        options.c_cflag &= ~PARENB;
    }

    // 设置停止位
    if (stopBits == 2) {
        options.c_cflag |= CSTOPB;
    } else {
        options.c_cflag &= ~CSTOPB;
    }
    options.c_cflag &= ~CRTSCTS;
    options.c_iflag &= ~(IXON | IXOFF | IXANY);

    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 10;

    // 应用配置
    if (tcsetattr(fd, TCSANOW, &options) != 0) {
        perror("tcsetattr failed");
        close(fd);
        return -1;
    }

    //打开串口后立即清空缓冲区
    tcflush(fd, TCIOFLUSH);

    return fd;
}

bool FxPlcInstance::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connected_) {
        //std::cout<<"PLC"<<device_.getPlcId()+"已连接";
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

    LOG_INFO("FX PLC [" + device_.getPlcId() + "] 连接成功");
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
    //仅清空输入缓冲区，丢弃所有历史残留响应
    tcflush(serialFd_, TCIFLUSH); 

    int n = write(serialFd_, sendBuf, sendLen);
    if (n != sendLen) {
        std::cerr << "发送失败" << std::endl;
        return false;
    }
    usleep(serialResponseDelayUs(device_.getBaudRate()));

    unsigned char firstByte;
    n = read(serialFd_, &firstByte, 1);
    if (n != 1) {
        std::cerr << "读取首字节超时或失败 (PLC无响应)" << std::endl;
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
                std::cerr << "读取数据不完整(超时)" << std::endl;
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
        tcflush(serialFd_, TCIFLUSH);
        return false;
    }
}
//读取s点
bool FxPlcInstance::readS(int sDecimal, bool& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_) return false;

    int addr = sDecimal /8; 
    int bitOffset= addr %8;
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
    if (recvLen < 3 || recvBuf[0] != 0x02) {
        std::cerr << "无效响应" << std::endl;
        return false;
    }
    char dataHex[3] = {
        static_cast<char>(recvBuf[1]),
        static_cast<char>(recvBuf[2]),
        '\0'
    };
    char* endptr;
    long val = strtol(dataHex, &endptr, 16);
    if (endptr == dataHex) {
        std::cerr << "状态值转换失败" << std::endl;
        return false;
    }

    state = (val >> bitOffset) & 1;
    return true;
  
}
bool FxPlcInstance::forceX(int xOctal, bool turnOn) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_) return false;
    // 与 forceY 对称：八进制地址 → 线性位（X45→37, X50→40, X77→63）
    int xDecimal = (xOctal / 10) * 8 + (xOctal % 10);
    int addr = 0x0400 + xDecimal;

    char addrLowStr[3], addrHighStr[3];
    sprintf(addrLowStr, "%02X", addr & 0xFF);       // 低8位
    sprintf(addrHighStr, "%02X", (addr >> 8) & 0xFF); // 高8位

    unsigned char cmd[11] = {0x02};
    cmd[1] = turnOn ? 0x37 : 0x38;   // 37=置位, 38=复位
    cmd[2] = addrLowStr[0];
    cmd[3] = addrLowStr[1];
    cmd[4] = addrHighStr[0];
    cmd[5] = addrHighStr[1];
    cmd[6] = 0x03;   // ETX
    cmd[7] = 0x00;   // 校验和占位
    cmd[8] = 0x00;

    calculateAndAppendSum(cmd, 1, 6);  

    unsigned char recvBuf[10];
    int recvLen = 0;
    if (sendAndReceive(cmd, 9, recvBuf, &recvLen)) {
        if (recvLen == 1 && recvBuf[0] == 0x06) {
            std::cout << "成功" << (turnOn ? "置位" : "复位")
                      << " X" << std::oct << xDecimal << std::dec << std::endl;
			LOG_INFO(std::string("[FxPlcInstance] PLC_WRITE_RESULT point=x") + std::to_string(xDecimal) + ", val=" + (turnOn ? "1" : "0") + ", result=ok");
            return true;
        } else if (recvLen == 1 && recvBuf[0] == 0x15) {
            std::cerr << "PLC拒绝操作 X 点 (返回NAK)" << std::endl;
			LOG_WARNING(std::string("[FxPlcInstance] PLC_WRITE_RESULT point=x") + std::to_string(xDecimal) + ", val=" + (turnOn ? "1" : "0") + ", result=FAIL, reason=plc_command_failed");
        } else {
            std::cerr << "收到未知响应" << std::endl;
        }
    }
    return false;
}
// 控制 Y 点
bool FxPlcInstance::forceY(int yOctal, bool turnOn) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_) return false;
    int yDecimal = (yOctal / 10) * 8 + (yOctal % 10);
    int addr =yDecimal + 0x500;
    
    char addrLowStr[3], addrHighStr[3];

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
    if (sendAndReceive(cmd, 9, recvBuf, &recvLen)) {
        if (recvLen == 1 && recvBuf[0] == 0x06) {
            std::cout << "成功" << (turnOn ? "置位" : "复位")
                      << " M" << mDecimal << std::endl;
			LOG_INFO(std::string("[FxPlcInstance] PLC_WRITE_RESULT point=m") + std::to_string(mDecimal) + ", val=" + (turnOn ? "1" : "0") + ", result=ok");
            return true;
        } else if (recvLen == 1 && recvBuf[0] == 0x15) {
            std::cerr << "PLC拒绝操作M点（返回NAK）" << std::endl;
			LOG_WARNING(std::string("[FxPlcInstance] PLC_WRITE_RESULT point=m") + std::to_string(mDecimal) + ", val=" + (turnOn ? "1" : "0") + ", result=FAIL, reason=plc_command_failed");
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

    int addr = 0x1000 + dNumber*2;
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

    char dataHex[5] = {static_cast<char>(recvBuf[3]), static_cast<char>(recvBuf[4]),
        static_cast<char>(recvBuf[1]), static_cast<char>(recvBuf[2]),
                        '\0'};
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
        std::cerr << "readYBit: " << yOctal << " Y地址超出范围（0-377八进制）" << std::endl;
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
        std::cerr << "readY: " << group << " 组号超出范围（0-9）" << std::endl;
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
//1字节读取
// bool FxPlcInstance::readM(int mDecimal, bool& state) {
//     std::lock_guard<std::mutex> lock(mutex_);
//     if (!connected_) return false;

//     int byteGroup = mDecimal / 8;
//     int bitOffset = mDecimal % 8;
//     if (byteGroup < 0 || byteGroup > 1023) {
//         std::cerr << "错误：M地址超出范围" << std::endl;
//         return false;
//     }

//     int byteAddr = 0x0100 + byteGroup;          // 字节地址
//     char addrStr[5];
//     sprintf(addrStr, "%04X", byteAddr);         // 高位在前

//     unsigned char cmd[11] = {
//         0x02,                // STX
//         0x30,                // 读命令
//         addrStr[0], addrStr[1], addrStr[2], addrStr[3],
//         '0', '1',            // 读取1个字节
//         0x03,                // ETX
//         0x00, 0x00           // 校验和占位
//     };

//     calculateAndAppendSum(cmd, 1, 8);   

//     unsigned char recvBuf[256];
//     int recvLen = 0;
//     if (!sendAndReceive(cmd, 11, recvBuf, &recvLen)) {
//         std::cerr << "发送/接收失败" << std::endl;
//         return false;
//     }

//     if (recvLen < 6 || recvBuf[0] != 0x02) {
//         std::cerr << "无效响应" << std::endl;
//         return false;
//     }

//     char dataHex[3] = {
//         static_cast<char>(recvBuf[1]),
//         static_cast<char>(recvBuf[2]),
//         '\0'
//     };
//     char *endptr;
//     long val = strtol(dataHex, &endptr, 16);
//     if (endptr == dataHex) {
//         std::cerr << "状态值转换失败" << std::endl;
//         return false;
//     }

//     int status8 = static_cast<int>(val);
//     state = (status8 >> bitOffset) & 1;
//     return true;
// }
bool FxPlcInstance::readM(int mDecimal, bool& state) {
   std::lock_guard<std::mutex> lock(mutex_);
   if (!connected_) return false;

   int wordAddr = 0x0100 + (mDecimal / 16)*2;
   int bitOffset = mDecimal % 16;
   char addrStr[5];
   sprintf(addrStr, "%04X", wordAddr);

   unsigned char cmd[11] = {
       0x02, 0x30,                    // STX + 读命令
       addrStr[0], addrStr[1],        // 高位在前
       addrStr[2], addrStr[3],
       '0', '2',                      // 读取 2 字节
       0x03,                          // ETX
       0x00, 0x00                     // 校验和占位
   };

   calculateAndAppendSum(cmd, 1, 8);  

   unsigned char recvBuf[256];
   int recvLen = 0;
   if (!sendAndReceive(cmd, 11, recvBuf, &recvLen)) {
       std::cerr << "发送/接收失败" << std::endl;
       return false;
   }

   if (recvLen < 8 || recvBuf[0] != 0x02) {
       return false;   // 无效响应
   }
   char dataHex[5] = {
       static_cast<char>(recvBuf[1]),
       static_cast<char>(recvBuf[2]),
       static_cast<char>(recvBuf[3]),
       static_cast<char>(recvBuf[4]),
       '\0'
   };

   int rawdata = strtol(dataHex, NULL, 16);

   int data = ((rawdata & 0x00FF) << 8) | ((rawdata & 0xFF00) >> 8);
   state = (data >> bitOffset) & 1;
   return true;
}

// //读取单个y点

bool FxPlcInstance::readXBit(int xOctal, bool& state) {
    int group = xOctal / 10;          // 组号，例如 X36 -> group=3
    int bitOffset = xOctal % 10;      // 位偏移，X36 -> bit=6

    if (group < 0 || group > 9) {
        std::cerr << "readXBit: " << xOctal << " X地址超出范围（0-77八进制）" << std::endl;
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
        std::cerr << "readX: " << group << " X组号超出范围（0-7）" << std::endl;
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
