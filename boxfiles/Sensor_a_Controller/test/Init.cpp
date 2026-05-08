#include "PLCConfig.h"
#include "Init.h"
#include <termios.h>
#include <stdio.h>
#include <iostream>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

// 串口底层配置
bool InitSerial::ConfigureSerial(int fd){
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        perror("串口配置失败（tcgetattr）");
        return false;
    }

    cfsetospeed(&tty, B9600);
    cfsetispeed(&tty, B9600);
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= CREAD | CLOCAL;
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    tty.c_oflag = 0;
    tty.c_lflag = 0;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 10;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("串口参数设置失败（tcsetattr）");
        return false;
    }
    return true;
}

// 单串口初始化
bool InitSerial::initSerial(){
    if (SerialPortStatus >= 0) {
        cout << "串口已处于初始化状态：" << portPath << endl;
        return true;
    }

    SerialPortStatus = open(portPath.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (SerialPortStatus < 0) {
        perror("串口打开失败");
        return false;
    }

    if (!ConfigureSerial(SerialPortStatus)) {
        close(SerialPortStatus);
        SerialPortStatus = -1;
        return false;
    }

    cout << "串口初始化成功（设备：" << portPath << "），fd=" << SerialPortStatus << endl;
    serialFdMap[portPath] = SerialPortStatus;

    return true;
}

// PLC上电检测
bool InitSerial::CheckPLCOnline() {
    int fd = this->SerialPortStatus;
    if (fd < 0) {
        cerr << "错误：串口未初始化，无法检测PLC状态" << endl;
        return false;
    }

    unsigned char sendBuf[] = {0x01, 0x01, 0x0F, 0x00, 0x00, 0x01, 0xFE, 0xDE};
    ssize_t sent = write(fd, sendBuf, sizeof(sendBuf));
    if (sent != (ssize_t)sizeof(sendBuf)) {
        perror("PLC上电检测指令发送失败");
        return false;
    }

    cout << "PLC上电检测指令已发送：";
    for (size_t i = 0; i < sizeof(sendBuf); ++i)
        printf("%02X ", sendBuf[i]);
    cout << endl;

    unsigned char recvBuf[256];
    ssize_t len = read(fd, recvBuf, sizeof(recvBuf));
    if (len > 0) {
        cout << "接收响应（" << len << "字节）：";
        for (size_t i = 0; i < (size_t)len; ++i)
            printf("%02X ", recvBuf[i]);
        cout << endl;

        if (len >= 4) {
            if (recvBuf[3] & 0x01) {
                cout << "PLC当前状态：已上电（M8000=ON）" << endl;
                return true;
            } else {
                cout << "PLC当前状态：未上电或离线（M8000=OFF）" << endl;
                return false;
            }
        } else {
            cout << "PLC上电检测响应无效" << endl;
            return false;
        }
    } else {
        cout << "未接收到PLC上电检测响应" << endl;
        return false;
    }
}

// 单串口关闭
void InitSerial::CloseSerial() {
    if (SerialPortStatus >= 0) {
        close(SerialPortStatus);
        SerialPortStatus = -1;
        cout << "串口已关闭：" << portPath << endl;
    }
}

// 批量初始化所有串口
bool InitSerial::InitAllSerial(const vector<PLCInfo>& plcList) {
    if (plcList.empty()) {
        cerr << "错误：无有效PLC配置，无法批量初始化串口" << endl;
        return false;
    }

    bool allSuccess = true;
    for (const auto& plc : plcList) {
        cout << "\n开始初始化串口：" << plc.serial_port << "（PLC" << plc.plc_id << "：" << plc.description << "）" << endl;
        InitSerial singleSerial(plc.serial_port);
        if (singleSerial.initSerial()) {
            plcSerialMap[plc.plc_id] = singleSerial.SerialPortStatus;
            cout << "PLC" << plc.plc_id << " 串口初始化成功，fd=" << singleSerial.SerialPortStatus << endl;
        } else {
            cerr << "PLC" << plc.plc_id << " 串口初始化失败，跳过该设备！" << endl;
            plcSerialMap[plc.plc_id] = -1;
            allSuccess = false;
        }
    }

    cout << "\n 批量串口初始化完成！共处理" << plcList.size() << "个PLC，有效fd数：" << plcSerialMap.size() << endl;
    return allSuccess;
}

// plc获取fd的唯一接口
int InitSerial::GetFdByPlcId(int plcId) {
    auto iter = plcSerialMap.find(plcId);
    if (iter != plcSerialMap.end() && iter->second >= 0) {
        return iter->second;
    }
    cerr << "错误：PLC" << plcId << " 未初始化/串口失败，无有效fd" << endl;
    return -1;
}

// 备用接口，通过串口路径获取fd
int InitSerial::GetFdByPort(const string& port) {
    auto iter = serialFdMap.find(port);
    if (iter != serialFdMap.end() && iter->second >= 0) {
        return iter->second;
    }
    cerr << "错误：串口" << port << " 未初始化/失败，无有效fd" << endl;
    return -1;
}

// 程序退出时统一关闭所有串口，释放所有资源
void InitSerial::CloseAllSerial() {
    cout << "\n开始统一关闭所有串口..." << endl;
    for (const auto& pair : serialFdMap) {
        const string& port = pair.first;
        int fd = pair.second;
        if (fd >= 0) {
            close(fd);
            cout << "串口已关闭：" << port << "（fd=" << fd << "）" << endl;
        }
    }
    serialFdMap.clear();
    plcSerialMap.clear();
    cout << "✅ 所有串口已全部关闭，资源释放完成！" << endl;
}
//静态成员变量初始化
map<string, int> InitSerial::serialFdMap;
map<int, int> InitSerial::plcSerialMap;