#include <iostream>    //可运行的程序，可以从最新的device.json文件中读取信息，然后得到正确的烟感的信息查询
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

int serialFd = -1;
json globalConfig;

vector<unsigned char> hexStringsToBytes(const vector<string>& hexStrings)
{
    vector<unsigned char> bytes;

    for (auto &s : hexStrings)
    {
        bytes.push_back((unsigned char)stoul(s,nullptr,16));
    }

    return bytes;
}

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
        return false;
    }

    cout<<"串口初始化成功: "<<portName<<endl;

    return true;
}

bool readSmoke(const json& compConfig)
{
    vector<unsigned char> sendBuf =
        hexStringsToBytes(compConfig["commands"]["query"]);

    write(serialFd,sendBuf.data(),sendBuf.size());

    tcdrain(serialFd);

    cout<<"Send Query: ";

    for(auto b:sendBuf)
        printf("%02X ",b);

    cout<<endl;

    this_thread::sleep_for(chrono::milliseconds(200));

    unsigned char recvBuf[256];

    memset(recvBuf,0,sizeof(recvBuf));

    int len=read(serialFd,recvBuf,sizeof(recvBuf));

    if(len<=0)
    {
        cout<<"No response"<<endl;
        return false;
    }

    cout<<"Recv: ";

    for(int i=0;i<len;i++)
        printf("%02X ",recvBuf[i]);

    cout<<endl;

    if(len>=4 && recvBuf[1]==0x01)
    {
        int state=recvBuf[3];

        if(state==0)
            cout<<"烟感报警!!!"<<endl;
        else
            cout<<"烟感状态: 正常"<<endl;
    }

    return true;
}

int main()
{
    InitEncoding();

    ifstream f("config.json");

    if(!f.is_open())
    {
        cout<<"config.json 打开失败"<<endl;
        return 0;
    }

    f>>globalConfig;

    string compId="smoke_alarm_1";

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
        cout<<"未找到烟感设备"<<endl;
        return 0;
    }

    auto &plc=globalConfig["plc_devices"][plcId];

    auto &comp=plc["components"][compId];

    string port=plc["bind_serial_port"];

    cout<<"设备: "<<comp["name"]<<endl;
    cout<<"PLC: "<<plcId<<endl;
    cout<<"串口: "<<port<<endl;

    if(!initSerial(port))
        return 0;

    int interval=globalConfig.value("polling_interval_ms",1000);

    cout<<"开始监测烟感..."<<endl;

    while(true)
    {
        readSmoke(comp);

        this_thread::sleep_for(
            chrono::milliseconds(interval)
        );

        cout<<"-----------------------"<<endl;
    }

    return 0;
}