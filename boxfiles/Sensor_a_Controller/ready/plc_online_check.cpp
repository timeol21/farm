#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <chrono>
#include <thread>
#include <fstream>
#include <vector>
#include <iomanip>
#include <cstdint>
#include "nlohmann/json.hpp"

using namespace std;
using json = nlohmann::json;

int serialFd = -1;
json globalConfig;

// 16进制字符串转字节数组（复用原有逻辑）
vector<unsigned char> hexStringsToBytes(const vector<string>& hexStrings)
{
    vector<unsigned char> bytes;

    for (auto &s : hexStrings)
    {
        bytes.push_back((unsigned char)stoul(s,nullptr,16));
    }

    return bytes;
}

// MODBUS标准CRC16计算函数（核心：自动计算所有PLC的CRC）
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
                crc >>= 1;
        }
    }
    return crc;
}

// 编码初始化（复用原有逻辑）
void InitEncoding()
{
    const char* locales[] = {"zh_CN.UTF-8","en_US.UTF-8","C.UTF-8","POSIX"};

    for(auto loc:locales)
    {
        if(setlocale(LC_ALL,loc)!=NULL)
        {
            cout<<"编码初始化成功: "<<loc<<endl;
            return;
        }
    }
}

// 串口配置（复用原有逻辑）
bool configureSerial(int fd,const json& portConfig)
{
    struct termios tty;

    if(tcgetattr(fd,&tty)!=0)
    {
        perror("tcgetattr");
        return false;
    }

    int baud=portConfig.value("baud_rate",9600);

    speed_t speed=B9600;

    if(baud==19200) speed=B19200;
    if(baud==115200) speed=B115200;

    cfsetospeed(&tty,speed);
    cfsetispeed(&tty,speed);

    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;

    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= CREAD | CLOCAL;

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);

    tty.c_lflag = 0;
    tty.c_oflag = 0;

    int timeout=portConfig.value("read_timeout_ms",1000);

    tty.c_cc[VMIN]=0;
    tty.c_cc[VTIME]=timeout/100;

    if(tcsetattr(fd,TCSANOW,&tty)!=0)
    {
        perror("tcsetattr");
        return false;
    }

    return true;
}

// 串口初始化（复用原有逻辑）
bool initSerial(const string& portName)
{
    // 关闭原有串口（避免重复打开）
    if (serialFd > 0) 
    {
        close(serialFd);
        serialFd = -1;
    }

    serialFd=open(portName.c_str(),O_RDWR | O_NOCTTY | O_SYNC);

    if(serialFd<0)
    {
        perror(("open serial " + portName).c_str());
        return false;
    }

    json portConfig;

    if(globalConfig["system_config"]["serial_ports"].contains(portName))
    {
        portConfig=globalConfig["system_config"]["serial_ports"][portName];
    }

    if(!configureSerial(serialFd,portConfig))
    {
        close(serialFd);
        serialFd = -1;
        return false;
    }

    cout<<"串口初始化成功: "<<portName<<endl;

    return true;
}

// 发送心跳指令并验证响应（通用逻辑抽离）
bool sendAndCheckHeartbeat(vector<unsigned char>& sendBuf, int plcSlaveAddr)
{
    // 发送指令
    write(serialFd, sendBuf.data(), sendBuf.size());
    tcdrain(serialFd);
    cout<<"【心跳查询法】发送检测指令: ";
    for(auto b:sendBuf) printf("%02X ",b);
    cout<<endl;

    // 清空接收缓冲区残留数据
    tcflush(serialFd, TCIFLUSH);
    // 等待PLC响应
    this_thread::sleep_for(chrono::milliseconds(100));
    
    unsigned char recvBuf[64];
    memset(recvBuf, 0, sizeof(recvBuf));
    int len=read(serialFd, recvBuf, sizeof(recvBuf));

    if(len <= 0)
    {
        cout<<"【心跳查询法】无响应，PLC离线"<<endl;
        return false;
    }

    // 验证响应合法性：首字节=从机地址 + 功能码=01 + 长度≥3
    cout<<"【心跳查询法】收到响应: ";
    for(int i=0;i<len;i++) printf("%02X ",recvBuf[i]);
    cout<<endl;
    
    if (len >=3 && recvBuf[0] == plcSlaveAddr && recvBuf[1] == 0x01)
    {
        cout<<"【心跳查询法】验证通过，PLC在线"<<endl;
        return true;
    }
    else
    {
        cout<<"【心跳查询法】响应非法，PLC离线/通信异常"<<endl;
        return false;
    }
}

// 心跳查询法（纯自动：从JSON读地址+实时算CRC）
bool plcOnlineCheck_Heartbeat(int plcSlaveAddr)
{
    // 1. 构造基础MODBUS指令（读线圈：0000地址，1个数量）
    vector<unsigned char> sendBuf;
    sendBuf.push_back(plcSlaveAddr);    // 从机地址（JSON读取）
    sendBuf.push_back(0x01);            // 功能码：读线圈
    sendBuf.push_back(0x00);            // 起始地址高8位
    sendBuf.push_back(0x00);            // 起始地址低8位
    sendBuf.push_back(0x00);            // 读取数量高8位
    sendBuf.push_back(0x01);            // 读取数量低8位

    // 2. 自动计算CRC（核心：无需写死，适配任意PLC地址）
    uint16_t crc = modbus_crc16(sendBuf.data(), sendBuf.size());
    sendBuf.push_back(crc & 0xFF);        // CRC低8位
    sendBuf.push_back((crc >> 8) & 0xFF); // CRC高8位

    // 3. 发送指令并验证响应
    return sendAndCheckHeartbeat(sendBuf, plcSlaveAddr);
}

// 批量检测所有PLC（完全从JSON自动读取，无写死逻辑）
void batchCheckAllPLC()
{
    cout<<"\n==================== 开始批量检测所有PLC ===================="<<endl;
    
    // 遍历JSON中所有PLC设备（自动识别数量，适配任意PLC）
    for (auto &plc : globalConfig["plc_devices"].items())
    {
        string plcId = plc.key();
        json plcConfig = plc.value();
        
        // 1. 从JSON解析PLC参数（无写死）
        string slaveAddrStr = plcConfig["slave_addr"]; // 如"0x01"
        int plcSlaveAddr = stoul(slaveAddrStr.substr(2), nullptr, 16); // 转十进制
        string portName = plcConfig["bind_serial_port"]; // 如"/dev/ttyS4"
        string plcDesc = plcConfig["description"];

        cout<<"\n------------------- 检测PLC: "<<plcId<<" ("<<plcDesc<<") -------------------"<<endl;
        
        // 2. 初始化对应串口
        if (!initSerial(portName))
        {
            cout<<plcId<<" 串口初始化失败，跳过检测"<<endl;
            continue;
        }

        // 3. 执行心跳检测（自动适配当前PLC地址）
        bool isOnline = plcOnlineCheck_Heartbeat(plcSlaveAddr);
        cout<<plcId<<" 最终状态："<<(isOnline ? "【在线】" : "【离线】")<<endl;
    }
    
    cout<<"\n==================== 批量检测完成 ===================="<<endl;
    
    // 关闭最后一个串口
    if (serialFd > 0) 
    {
        close(serialFd);
        serialFd = -1;
    }
}

int main()
{
    // 1. 初始化编码
    InitEncoding();

    // 2. 读取JSON配置文件（固定路径：config.json）
    ifstream f("config.json");
    if(!f.is_open())
    {
        cout<<"config.json 打开失败"<<endl;
        return 0;
    }
    f>>globalConfig;
    cout<<"JSON配置文件读取成功"<<endl;

    // 3. 批量检测所有PLC（核心：自动识别JSON中所有PLC）
    batchCheckAllPLC();

    return 0;
}

// //g++ plc_online_check.cpp -o plc_online_check -std=c++11