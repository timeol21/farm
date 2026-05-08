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
#include "json.hpp"

using namespace std;
using json = nlohmann::json;

// 全局变量：串口文件描述符、JSON配置（和烟感/红外程序一致）
int serialFd = -1;
json globalConfig;

// 工具函数：十六进制字符串转字节数组（兼容JSON中0x前缀格式）
vector<unsigned char> hexStringsToBytes(const vector<string>& hexStrings)
{
    vector<unsigned char> bytes;
    for (auto &s : hexStrings)
    {
        // 去除0x/0X前缀，适配JSON中的指令格式
        string hexStr = s;
        if (hexStr.substr(0, 2) == "0x" || hexStr.substr(0, 2) == "0X") {
            hexStr = hexStr.substr(2);
        }
        bytes.push_back((unsigned char)stoul(hexStr, nullptr, 16));
    }
    return bytes;
}

// 编码初始化（统一风格）
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
    perror("所有编码设置均失败，可能导致打印乱码");
}

// 串口配置函数（从JSON读取参数，统一配置逻辑）
bool configureSerial(int fd,const json& portConfig)
{
    struct termios tty;
    if(tcgetattr(fd,&tty)!=0)
    {
        perror("tcgetattr");
        return false;
    }

    // 从JSON读取波特率（默认9600）
    int baud=portConfig.value("baud_rate",9600);
    speed_t speed=B9600;
    if(baud==19200) speed=B19200;
    if(baud==115200) speed=B115200;

    cfsetospeed(&tty,speed);
    cfsetispeed(&tty,speed);

    // 8数据位、无校验、1停止位（和JSON配置匹配）
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;

    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= CREAD | CLOCAL;

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    tty.c_lflag = 0;
    tty.c_oflag = 0;

    // 从JSON读取超时时间（默认1000ms）
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

// 串口初始化（读取JSON中绑定的串口，统一初始化逻辑）
bool initSerial(const string& portName)
{
    serialFd=open(portName.c_str(),O_RDWR | O_NOCTTY | O_SYNC);
    if(serialFd<0)
    {
        perror("open serial");
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

// 水位传感器状态查询核心函数（和烟感/红外逻辑对齐）
bool readWaterLevel(const json& compConfig)
{
    if (serialFd < 0) {
        cerr << "错误：串口未初始化" << endl;
        return false;
    }

    // 从JSON读取查询指令并转换为字节
    vector<unsigned char> sendBuf = hexStringsToBytes(compConfig["commands"]["query"]);

    // 发送查询指令
    ssize_t sent = write(serialFd, sendBuf.data(), sendBuf.size());
    if (sent != (ssize_t)sendBuf.size()) {
        perror("write (water level query)");
        return false;
    }
    tcdrain(serialFd); // 等待数据发送完成

    // 打印发送的指令（十六进制）
    cout<<"Send Water Level Query: ";
    for(auto b:sendBuf)
        printf("%02X ",b);
    cout<<endl;

    // 等待PLC响应
    this_thread::sleep_for(chrono::milliseconds(100));

    // 读取响应数据
    unsigned char recvBuf[256];
    memset(recvBuf,0,sizeof(recvBuf));
    int len=read(serialFd,recvBuf,sizeof(recvBuf));

    if(len<=0)
    {
        cout<<"水位传感器：无响应（超时）"<<endl;
        return false;
    }

    // 打印接收的响应
    cout<<"Recv Water Level: ";
    for(int i=0;i<len;i++)
        printf("%02X ",recvBuf[i]);
    cout<<endl;

    // 解析水位传感器状态（保留你原代码的判断逻辑）
    if(len>=4 && recvBuf[1]==0x01)
    {
        int state=recvBuf[3];
        cout<<"水位传感器状态: ";
        if(state==0) {
            cout<<"低电平（未触发，水位正常）"<<endl;
        } else if(state==1) {
            cout<<"高电平（触发，水位异常）"<<endl;
        } else {
            cout<<"未知状态（值："<<(int)state<<"）"<<endl;
        }
    } else {
        cout<<"水位传感器：响应格式无效"<<endl;
    }

    return true;
}

// 关闭串口（规范退出）
void closeSerial() {
    if (serialFd >= 0) {
        close(serialFd);
        serialFd = -1;
        cout << "串口已关闭" << endl;
    }
}

int main()
{
    // 1. 初始化编码
    InitEncoding();

    // 2. 读取JSON配置文件
    ifstream f("config.json");
    if(!f.is_open())
    {
        cout<<"config.json 打开失败"<<endl;
        return 1;
    }
    f>>globalConfig;

    // 3. 查找水位传感器设备（对应JSON里的water_level_sensor_1）
    string compId="water_level_sensor_1";
    string plcId;
    bool found=false;

    for(auto &plc:globalConfig["plc_devices"].items())
    {
        if(plc.value()["components"].contains(compId))
        {
            plcId=plc.key();
            found=true;
            break;
        }
    }

    if(!found)
    {
        cout<<"未找到水位传感器设备（"<<compId<<"）"<<endl;
        return 1;
    }

    // 4. 获取设备和串口配置
    auto &plc=globalConfig["plc_devices"][plcId];
    auto &comp=plc["components"][compId];
    string port=plc["bind_serial_port"];

    // 打印设备信息（和烟感/红外程序一致）
    cout<<"设备: "<<comp["name"]<<endl;
    cout<<"PLC: "<<plcId<<endl;
    cout<<"串口: "<<port<<endl;
    cout<<"MODBUS地址: "<<comp["reg_addr"]<<endl;
    cout<<"PLC端口: "<<comp["plc_port"]<<endl;

    // 5. 初始化串口
    if(!initSerial(port)) {
        return 1;
    }

    // 6. 获取轮询间隔（从JSON读取，默认1000ms）
    int interval=globalConfig.value("polling_interval_ms",1000);

    // 7. 循环监测水位传感器
    cout<<"=== 水位传感器监测已启动 ==="<<endl;
    cout<<"轮询间隔: "<<interval<<"ms | 按Ctrl+C退出"<<endl;
    cout<<"通信协议: MODBUS RTU | 波特率: "<<globalConfig["system_config"]["serial_ports"][port]["baud_rate"]<<endl;
    cout<<"============================"<<endl<<endl;

    while(true)
    {
        cout<<"--- 轮询周期开始 ---"<<endl;
        readWaterLevel(comp); // 查询水位传感器状态
        this_thread::sleep_for(chrono::milliseconds(interval));
        cout<<"-----------------------"<<endl<<endl;
    }

    // 8. 关闭串口（实际通过Ctrl+C退出，此处为规范写法）
    closeSerial();
    return 0;
}