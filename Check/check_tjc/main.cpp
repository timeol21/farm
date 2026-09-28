// 编译：g++ -std=c++11 tjc_serial.cpp -o tjc_serial
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

class TjcSerial {
public:
    int openPort(const char* dev, speed_t baud = B9600) {
        fd_ = ::open(dev, O_RDWR | O_NOCTTY | O_NDELAY);
        if (fd_ < 0) { perror("open"); return -1; }
        fcntl(fd_, F_SETFL, 0);            // 改为由 VTIME 控制超时

        struct termios tio;
        if (tcgetattr(fd_, &tio) != 0) { perror("tcgetattr"); return -1; }

        cfmakeraw(&tio);                   // raw 模式，关闭回显/行缓冲
        tio.c_cflag |= (CLOCAL | CREAD);   // 忽略调制解调器线，允许接收
        tio.c_cflag &= ~

CSIZE;
        tio.c_cflag |= CS8;                // 8 位数据位
        tio.c_cflag &= ~PARENB;            // 无校验
        tio.c_cflag &= ~CSTOPB;            // 1 位停止位
        tio.c_cflag &= ~CRTSCTS;           // 关硬件流控
        tio.c_cflag &= ~HUPCL;             // 关闭时不动 DTR
        tio.c_iflag &= ~(IXON | IXOFF | IXANY); // 关软件流控

        cfsetispeed(&tio, baud);
        cfsetospeed(&tio, baud);

        tio.c_cc[VMIN]  = 0;               // 读超时模式
        tio.c_cc[VTIME] = 10;              // 1 秒

        tcflush(fd_, TCIOFLUSH);
        if (tcsetattr(fd_, TCSANOW, &tio) != 0) { perror("tcsetattr"); return -1; }
        return 0;
    }

    // 发送指令，自动补 FF FF FF，默认间隔 100ms
    int writeCmd(const std::string& cmd, int gap_ms = 100) {
        if (fd_ < 0) return -1;
        std::vector<unsigned char> buf(cmd.begin(), cmd.end());
        buf.push_back(0xFF); buf.push_back(0xFF); buf.push_back(0xFF);
        ssize_t n = ::write(fd_, buf.data(), buf.size());
        if (gap_ms > 0) usleep(gap_ms * 1000);

        return (n == (ssize_t)buf.size()) ? 0 : -1;
    }

    // 简单读取一次返回（生产环境请做按帧拼接）
    std::string readBack() {
        if (fd_ < 0) return std::string();
        char tmp[256];
        std::string data;
        ssize_t n;
        while ((n = ::read(fd_, tmp, sizeof(tmp))) > 0) {
            data.append(tmp, n);
            if (data.size() >= 4) break;
        }
        return data;
    }

    void closePort() { if (fd_ >= 0) { ::close(fd_); fd_ = -1; } }

private:
    int fd_ = -1;
};

int main() {
    TjcSerial s;
    if (s.openPort("/dev/ttyUSB0", B9600) != 0) return 1;

    s.writeCmd("page main");
    s.writeCmd("t0.txt=\"hello123\"");
    s.writeCmd("n0.val=456");

    std::string r = s.readBack();
    for (size_t i = 0; i < r.size(); ++i)
        printf("%02X ", (unsigned char)r[i]);
    printf("\n");

    s.closePort();
    return 0;
}