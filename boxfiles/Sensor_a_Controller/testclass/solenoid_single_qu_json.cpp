#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <chrono>
#include <thread>
#include <fstream>
#include <vector>
#include "json.hpp"

using namespace std;
using json = nlohmann::json;

// 全局变量
int serialFd = -1;
json globalConfig;

void InitEncoding() {
    const char* locales[] = {"zh_CN.UTF-8", "en_US.UTF-8", "C.UTF-8", "POSIX"};
    for (const char* loc : locales) {
        if (setlocale(LC_ALL, loc) != NULL) {
            cout << "编码初始化成功：使用 " << loc << " 编码" << endl;
            return;
        }
    }
}

// 十六进制字符串数组转换为字节数组
vector<unsigned char> hexStringsToBytes(const vector<string>& hexStrings) {
    vector<unsigned char> bytes;
    for (const string& s : hexStrings) {
        bytes.push_back(static_cast<unsigned char>(stoul(s, nullptr, 16)));
    }
    return bytes;
}

// 串口配置函数
bool configureSerial(int fd, const json& portConfig) {
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        perror("tcgetattr");
        return false;
    }

    int baudRate = portConfig.value("baud_rate", 9600);
    int dataBits = portConfig.value("data_bits", 8);
    int stopBits = portConfig.value("stop_bits", 1);
    string parity = portConfig.value("parity", "none");
    int timeoutMs = portConfig.value("read_timeout_ms", 200);

    speed_t speed;
    switch (baudRate) {
        case 9600: speed = B9600; break;
        case 19200: speed = B19200; break;
        case 115200: speed = B115200; break;
        default: speed = B9600;
    }
    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);

    tty.c_cflag &= ~CSIZE;
    if (dataBits == 8) tty.c_cflag |= CS8;
    else if (dataBits == 7) tty.c_cflag |= CS7;

    if (parity == "none") tty.c_cflag &= ~PARENB;
    else if (parity == "even") { tty.c_cflag |= PARENB; tty.c_cflag &= ~PARODD; }
    else if (parity == "odd") { tty.c_cflag |= PARENB; tty.c_cflag |= PARODD; }

    if (stopBits == 1) tty.c_cflag &= ~CSTOPB;
    else if (stopBits == 2) tty.c_cflag |= CSTOPB;

    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= CREAD | CLOCAL;
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    tty.c_oflag = 0;
    tty.c_lflag = 0;

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = timeoutMs / 100;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
        return false;
    }
    return true;
}

bool initSerialFromConfig(const string& portPath) {
    if (!globalConfig.contains("system_config") || 
        !globalConfig["system_config"].contains("serial_ports") ||
        !globalConfig["system_config"]["serial_ports"].contains(portPath)) {
        cerr << "Error: Config for port " << portPath << " not found" << endl;
        return false;
    }

    const auto& portConfig = globalConfig["system_config"]["serial_ports"][portPath];
    serialFd = open(portPath.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (serialFd < 0) {
        perror("open serial port");
        return false;
    }

    if (!configureSerial(serialFd, portConfig)) {
        close(serialFd);
        serialFd = -1;
        return false;
    }

    cout << "Serial port " << portPath << " initialized from config." << endl;
    return true;
}

bool sendCommand(const string& plcId, const string& componentId, const string& cmdKey) {
    if (serialFd < 0) return false;

    const auto& component = globalConfig["plc_devices"][plcId]["components"][componentId];
    vector<unsigned char> sendBuf = hexStringsToBytes(component["commands"][cmdKey]);
    
    if (write(serialFd, sendBuf.data(), sendBuf.size()) != (ssize_t)sendBuf.size()) {
        perror("write");
        return false;
    }

    cout << "Sent [" << component["name"] << " " << cmdKey << "] command: ";
    for (auto b : sendBuf) printf("%02X ", b);
    cout << endl;

    unsigned char recvBuf[256];
    memset(recvBuf, 0, sizeof(recvBuf));
    ssize_t len = read(serialFd, recvBuf, sizeof(recvBuf));

    if (len > 0) {
        cout << "Response (" << len << " bytes): ";
        for (ssize_t i = 0; i < len; ++i) printf("%02X ", recvBuf[i]);
        cout << endl;
    }

    return true;
}

int main() {
    InitEncoding();

    ifstream configFile("config.json");
    if (!configFile.is_open()) {
        cerr << "Could not open config.json" << endl;
        return 1;
    }
    configFile >> globalConfig;

    string plcId = "plc_1";
    string compId = "solenoid_valve_1";
    string targetPort = globalConfig["plc_devices"][plcId]["bind_serial_port"];
    
    if (!initSerialFromConfig(targetPort)) return 1;

    int choice;
    while (true) {
        cout << "\n--- " << globalConfig["plc_devices"][plcId]["components"][compId]["name"] << " Control ---" << endl;
        cout << "1. Open" << endl;
        cout << "2. Close" << endl;
        cout << "3. Query" << endl;
        cout << "0. Exit" << endl;
        cout << "Choice: ";
        cin >> choice;

        if (choice == 0) break;
        
        string cmdKey;
        if (choice == 1) cmdKey = "open";
        else if (choice == 2) cmdKey = "close";
        else if (choice == 3) cmdKey = "query";
        else continue;

        sendCommand(plcId, compId, cmdKey);
    }

    if (serialFd >= 0) close(serialFd);
    return 0;
}
