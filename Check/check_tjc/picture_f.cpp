#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>


// 编译: g++ -std=c++11 tjc_send_image.cpp -o tjc_send_image
// 用法: ./tjc_send_image /dev/ttyUSB0 a.jpg ram/a.jpg


// g++ picture_f.cpp -o picture_f -std=c++11
// sudo ./picture_f /dev/ttyUSB0 1.jpg ram/a.jpg


class TjcSerial {
public:
    int openPort(const char* dev, speed_t baud = B115200) {


        fd_ = ::open(dev, O_RDWR | O_NOCTTY | O_NDELAY);
        if (fd_ < 0) { perror("open"); return -1; }
        fcntl(fd_, F_SETFL, 0);              // 改由 VTIME 控制超时

        struct termios tio;
        if (tcgetattr(fd_, &tio) != 0) { perror("tcgetattr"); return -1; }
        cfmakeraw(&tio);                     // raw 模式，必须
        tio.c_cflag |= (CLOCAL | CREAD);
        tio.c_cflag &= ~CSIZE;
        tio.c_cflag |= CS8;                  // 8 位数据位
        tio.c_cflag &= ~PARENB;              // 无校验
        tio.c_cflag &= ~CSTOPB;              // 1 位停止位
        tio.c_cflag &= ~CRTSCTS;             // 关硬件流控
        tio.c_cflag &= ~HUPCL;
        tio.c_iflag &= ~(IXON | IXOFF | IXANY); // 关软件流控
        cfsetispeed(&tio, baud);
        cfsetospeed(&tio, baud);
        tio.c_cc[VMIN]  = 0;
        tio.c_cc[VTIME] = 10;
        tcflush(fd_, TCIOFLUSH);
        if (tcsetattr(fd_, TCSANOW, &tio) != 0) { perror("tcsetattr"); return -1; }
        return 0;
    }

    int writeAll(const unsigned char* p, size_t n) {


        size_t off = 0;
        while (off < n) {
            ssize_t w = ::write(fd_, p + off, n - off);
            if (w <= 0) return -1;
            off += (size_t)w;
        }
        return 0;
    }

    // 普通指令，自动补 FF FF FF
    int writeCmd(const std::string& cmd, int gap_ms = 50) {
        std::vector<unsigned char> buf(cmd.begin(), cmd.end());
        buf.push_back(0xFF); buf.push_back(0xFF); buf.push_back(0xFF);
        if (writeAll(buf.data(), buf.size()) != 0) return -1;
        if (gap_ms > 0) usleep(gap_ms * 1000);
        return 0;
    }

    // 等屏幕返回：跳过结束符里的 0xFF，返回第一个有效字节；超时返回 -1
    int waitAck(int timeout_ms) {
        int c = readByte(timeout_ms);
        while (c == 0xFF) c = readByte(50);
        return c;
    }

    void closePort() { if (fd_ >= 0) { ::close(fd_); fd_ = -1; } }

private:
    int readByte(int timeout_ms) {
        fd_set rf; FD_ZERO(&rf); FD_SET(fd_, &rf);
        struct timeval tv;
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;


        int r = select(fd_ + 1, &rf, NULL, NULL, &tv);
        if (r <= 0) return -1;
        unsigned char c;
        if (::read(fd_, &c, 1) != 1) return -1;
        return (int)c;
    }
    int fd_ = -1;
};

static const size_t CHUNK = 4096;   // 单包数据上限 4096 字节

// 组包：12 字节包头 + 数据（无校验模式）
static void buildPacket(uint16_t id, const unsigned char* data, uint16_t len,
                        std::vector<unsigned char>& out) {
    out.clear();
    const unsigned char head[7] = {0x3A, 0xA1, 0xBB, 0x44, 0x7F, 0xFF, 0xFE};
    out.insert(out.end(), head, head + 7);
    out.push_back(0x00);                 // 校验类型：0x00 无校验
    out.push_back(id & 0xFF);            // 包ID 低字节
    out.push_back((id >> 8) & 0xFF);     // 包ID 高字节
    out.push_back(len & 0xFF);           // 数据大小 低字节
    out.push_back((len >> 8) & 0xFF);    // 数据大小 高字节
    out.insert(out.end(), data, data + len);
}

bool sendFileToScreen(TjcSerial& s, const std::string& path,
                      const std::vector<unsigned char>& file) {


    char cmd[160];
    snprintf(cmd, sizeof(cmd), "twfile \"%s\",%lu", path.c_str(),
             (unsigned long)file.size());
    if (s.writeCmd(cmd, 50) != 0) return false;

    int ack = s.waitAck(3000);
    if (ack != 0xFE) {
        printf("Enter transparent transmission failed, return 0x%02X (0x06 means create file failed)\n", ack & 0xFF);
        return false;
    }

    uint16_t id = 0;
    size_t off = 0;
    bool finished = false;

    while (off < file.size()) {
        size_t n = std::min(CHUNK, file.size() - off);
        std::vector<unsigned char> pkt;
        buildPacket(id, &file[off], (uint16_t)n, pkt);

        bool ok = false;
        for (int retry = 0; retry < 3 && !ok; ++retry) {
            if (s.writeAll(pkt.data(), pkt.size()) != 0) continue;
            int r = s.waitAck(500);
            if (r == 0x05)      { ok = true; }                       // 本包成功
            else if (r == 0xFD) { ok = true; finished = true; }       // 传输已结束
            else printf("Packet %u failed (0x%02X), resend same packet with same ID\n", id, r & 0xFF);
        }
        if (!ok) { printf("Packet %u retry failed, abort transfer\n", id); return false; }



        off += n;
        ++id;
        if (finished) break;
    }

    if (!finished) {
        int fin = s.waitAck(3000);
        if (fin != 0xFD) {
            printf("Transfer end 0xFD not received, return 0x%02X\n", fin & 0xFF);
            return false;
        }
    }
    printf("File transparent transmission completed\n");
    return true;
}

int main(int argc, char** argv) {
    const char* dev  = (argc > 1) ? argv[1] : "/dev/ttyUSB0";
    const char* img  = (argc > 2) ? argv[2] : "a.jpg";
    const char* path = (argc > 3) ? argv[3] : "ram/a.jpg";

    std::ifstream in(img, std::ios::binary);
    if (!in) { printf("Open image failed: %s\n", img); return 1; }
    std::vector<unsigned char> file((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
    printf("Image size: %lu bytes\n", (unsigned long)file.size());

    TjcSerial s;
    if (s.openPort(dev, B115200) != 0) return 1;   // 波特率必须与工程 baud 一致

    if (!sendFileToScreen(s, path, file)) return 1;

    // 让外部图片控件显示刚传上去的图（控件名按你工程实际填写）
    s.writeCmd(std::string("exp0.path=\"") + path + "\"");


    s.writeCmd("page main");

    s.closePort();
    return 0;
}