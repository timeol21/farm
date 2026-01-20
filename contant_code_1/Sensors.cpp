#include "Sensors.h"

// --- 烟雾传感器逻辑 ---
SmokeSensor::SmokeSensor() {}
void SmokeSensor::execute() {
    if (sm.fd < 0) sm.initPort(portPath);
    unsigned char cmd[] = {0x01, 0x01, 0x04, 0x01, 0x00, 0x01, 0xAD, 0x3A};
    sm.sendData(cmd, 8);
    unsigned char buf[255];
    int len = sm.recvData(buf);
    
    cout << "[" << type << " ID:" << id << "] 状态: ";
    if (len >= 4 && buf[3] == 0x01) cout << "检测到烟雾！" << endl;
    else cout << "安全" << endl;
}

// --- 水位传感器逻辑 ---
WaterSensor::WaterSensor() {}
void WaterSensor::execute() {
    if (sm.fd < 0) sm.initPort(portPath);
    unsigned char cmd[] = {0x01, 0x01, 0x04, 0x00, 0x00, 0x01, 0xFC, 0xFA};
    sm.sendData(cmd, 8);
    unsigned char buf[255];
    int len = sm.recvData(buf);

    cout << "[" << type << " ID:" << id << "] 状态: ";
    if (len >= 4 && buf[3] == 0x01) cout << "检测到水位溢出！" << endl;
    else cout << "正常" << endl;
}

// --- 温湿度传感器逻辑 ---
HumitureSensor::HumitureSensor() : currentTemp(0), currentHumi(0) {}
void HumitureSensor::execute() {
    if (sm.fd < 0) {
        if (!sm.initPort(portPath)) {
            cout << "[" << type << "] 无法初始化串口 " << portPath << endl;
            return;
        }
    }
    unsigned char cmd[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B};
    sm.sendData(cmd, 8);
    unsigned char buf[255];
    int len = sm.recvData(buf);
    
    if (len == 9) {
        currentHumi = ((buf[3] << 8) | buf[4]) / 10.0;
        currentTemp = ((buf[5] << 8) | buf[6]) / 10.0;
        cout << "[" << type << " ID:" << id << "] 温度: " << currentTemp << "C, 湿度: " << currentHumi << "%" << endl;
    } else if (len == 0) {
        cout << "[" << type << "] 警告: 传感器无响应 (USB可能已断开，尝试关闭以便下次重连)" << endl;
        sm.closePort(); // 读不到数据时主动关闭，下次 execute 会触发重新 initPort
    } else {
        // 如果没读到数据，也打印出来
        cout << "[" << type << " ID:" << id << "] 串口无响应或长度不对 (收到 " << len << " 字节)" << endl;
    }
}