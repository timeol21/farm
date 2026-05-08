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
#include "nlohmann/json.hpp"

using namespace std;
using json = nlohmann::json;

// 全局变量：串口文件描述符、JSON配置（和烟感程序一致）
int serialFd = -1;
json globalConfig;
json humitureConfig;

// 工具函数：十六进制字符串转字节数组（兼容0x前缀）
vector<unsigned char> hexStringsToBytes(const vector<string>& hexStrings)
{
    vector<unsigned char> bytes;
    for (auto &s : hexStrings)
    {
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

// 串口配置函数（从JSON读取参数）
bool configureSerial(int fd,const json& portConfig)
{
    struct termios tty;
    if (tcgetattr(fd,&tty)!=0)
    {
        perror("串口参数获取失败");
        return false;
    }

    // 从JSON读取波特率（默认9600）
    int baud=portConfig.value("baud_rate", 9600);
    speed_t speed=B9600;
    if(baud==19200) speed=B19200;
    if(baud==115200) speed=B115200;

    cfsetospeed(&tty,speed);
    cfsetispeed(&tty,speed);

    // 8数据位、无校验、1停止位（从JSON读取）
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
    int timeout=portConfig.value("read_timeout_ms", 1000);
    tty.c_cc[VMIN]=0;
    tty.c_cc[VTIME]=timeout/100;

    if (tcsetattr(fd,TCSANOW,&tty)!=0)
    {
        perror("串口参数配置失败");
        return false;
    }

    return true;
}

// 串口初始化（从JSON读取绑定串口）
bool initSerial(const string& portName)
{
    serialFd=open(portName.c_str(),O_RDWR | O_NOCTTY | O_SYNC);
    if(serialFd<0)
    {
        perror("串口打开失败（必须用sudo运行，检查设备节点是否正确）");
        return false;
    }

    json portConfig;
    if(globalConfig["system_config"]["serial_ports"].contains(portName))
    {
        portConfig=globalConfig["system_config"]["serial_ports"][portName];
    }

    if(!configureSerial(serialFd, portConfig))
    {
        close(serialFd);
        serialFd = -1;
        return false;
    }

    cout<<"串口初始化成功: "<<portName<<endl;
    return true;
}

// 温湿度读取核心函数（保留原有解析逻辑，从JSON读取指令）
bool readHumiture()
{
    if (serialFd < 0) {
        cerr << "错误：串口未初始化" << endl;
        return false;
    }

    // 从JSON读取查询指令并转换为字节
    vector<string> cmdStrings = humitureConfig["commands"]["query"].get<vector<string>>();
    vector<unsigned char> sendBuf = hexStringsToBytes(cmdStrings);

    // 发送读取指令
    ssize_t sent = write(serialFd, sendBuf.data(), sendBuf.size());
    if (sent != (ssize_t)sendBuf.size()) {
        perror("发送指令失败");
        return false;
    }

    // 打印发送的指令
    cout<<"发送读取指令：";
    for(auto b:sendBuf)
        printf("%02X ",b);
    cout<<endl;

    // 接收传感器响应
    unsigned char recvBuf[256] = {0};
    ssize_t len = read(serialFd, recvBuf, sizeof(recvBuf));

    // 打印完整接收报文
    cout << "接收完整报文（长度：" << len << "字节）：";
    for (ssize_t i = 0; i < len; ++i) {
        printf("%02X ", recvBuf[i]);
    }
    cout << endl;

    // 解析温湿度（保留原有核心逻辑）
    int slaveAddr = stoul(humitureConfig["slave_addr"].get<string>().substr(2), nullptr, 16);
    if (len == 9 && recvBuf[0] == slaveAddr && recvBuf[1] == 0x03) {
        uint16_t humiData = (recvBuf[3] << 8) | recvBuf[4];
        uint16_t tempData = (recvBuf[5] << 8) | recvBuf[6];
        float temperature = tempData / 10.0;
        float humidity = humiData / 10.0;

        cout << "解析成功！" << endl;
        cout << "温度：" << fixed << setprecision(1) << temperature << "℃ " 
             << "湿度：" << fixed << setprecision(1) << humidity << "%RH" << endl;
    } else if (len == 0) {
        cout << "未收到响应（检查接线、传感器地址、设备节点是否正确）" << endl;
    } else {
        cout << "响应异常（地址/功能码不匹配或格式错误）" << endl;
    }

    return true;
}

// 关闭串口
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

    // 3. 查找直连的温湿度传感器设备
    string devId="humiture_sensor_1";
    bool found=false;

    // 遍历linux_direct_devices下的serial_direct_devices
    for(auto &dev:globalConfig["linux_direct_devices"]["serial_direct_devices"])
    {
        if(dev["id"] == devId)
        {
            humitureConfig = dev;
            found = true;
            break;
        }
    }

    if(!found)
    {
        cout<<"未找到温湿度传感器设备（"<<devId<<"）"<<endl;
        return 1;
    }

    // 4. 获取串口配置
    string port=humitureConfig["bind_serial_port"];

    // 打印设备信息（和烟感程序一致）
    cout<<"================================ 设备配置信息 ================================"<<endl;
    cout<<"设备: "<<humitureConfig["name"]<<endl;
    cout<<"串口: "<<port<<endl;
    cout<<"从机地址: "<<humitureConfig["slave_addr"]<<endl;
    cout<<"起始寄存器: "<<humitureConfig["reg_addr"]<<endl;
    cout<<"读取寄存器数: "<<humitureConfig["read_regs"]<<endl;
    cout<<"描述: "<<humitureConfig["description"]<<endl;
    cout<<"=============================================================================="<<endl;

    // 5. 初始化串口
    if(!initSerial(port)) {
        return 1;
    }

    // 6. 获取轮询间隔（从JSON读取，默认1000ms）
    int interval=globalConfig.value("polling_interval_ms", 1000);

    // 7. 循环监测温湿度
    cout<<"=== 温湿度传感器监测已启动 ==="<<endl;
    cout<<"轮询间隔: "<<interval<<"ms | 按Ctrl+C退出"<<endl;
    cout<<"============================"<<endl<<endl;

    try {
        while(true)
        {
            cout<<"--- 轮询周期开始 ---"<<endl;
            readHumiture(); // 查询温湿度
            this_thread::sleep_for(chrono::milliseconds(interval));
            cout<<"---------------------"<<endl<<endl;
        }
    } catch(...) {
        // 捕获Ctrl+C等异常
        cout<<"\n程序退出中..."<<endl;
    }

    // 8. 关闭串口
    closeSerial();

    return 0;
}