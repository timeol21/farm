// fx_test_client_socket.cpp — vplc L2 端到端测试 (Unix socket 模式)
//
// 用法: ./fx_test_client_socket <socketPath> <basic|fault|forceX|forceM|all>...
//   <socketPath> 默认 /tmp/vplc.sock
//   scenario    可多个,一次连接跑完所有(避免反复连接)
//
// 协议帧: FX 编程口 (FX3U),严格对齐 washcar fx_plc_instance.cpp
//   请求: STX(0x02) + CMD(1B) + ADDR(4B hex) + LEN(2B hex) + ETX(0x03) + SUM(2B)
//   响应:
//     ACK    0x06                   (1B)
//     NAK    0x15                   (1B)
//     数据   STX + DATA(N×2B) + ETX + SUM(2B)
//
// 校验和:从 CMD 到 ETX 含 ETX 累加, %256, 大写 2 位 hex ASCII
//   对应 fx_plc_instance.cpp:132-141 calculateAndAppendSum(cmd, 1, 8)
//
// 退出码: 0=全过, 1=某项失败, 2=连接失败
#include <sys/socket.h>
#include <sys/un.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

static int g_pass = 0, g_fail = 0;

#define EXPECT(cond, label) do { \
    if (cond) { g_pass++; printf("  [PASS] %s\n", label); } \
    else      { g_fail++; printf("  [FAIL] %s\n", label); } \
} while (0)

constexpr unsigned char STX = 0x02, ETX = 0x03, ACK = 0x06, NAK = 0x15;
constexpr unsigned char CMD_READ = 0x30, CMD_SET = 0x37, CMD_RST = 0x38;

static void append_sum(unsigned char* buf, int start, int len) {
    // 对应 fx_plc_instance.cpp:132-141
    unsigned int sum = 0;
    for (int i = 0; i < len; i++) sum += buf[start + i];
    snprintf(reinterpret_cast<char*>(buf + start + len), 3, "%02X", sum & 0xFF);
}

static int build_read_d(unsigned char* buf, int d) {
    int addr = 0x1000 + d * 2;
    buf[0] = STX; buf[1] = CMD_READ;
    snprintf(reinterpret_cast<char*>(buf + 2), 5, "%04X", addr);
    buf[6] = '0'; buf[7] = '1';  // LEN 1 word
    buf[8] = ETX;
    append_sum(buf, 1, 8);
    return 11;
}

static int build_read_m_word(unsigned char* buf, int m) {
    int addr = 0x0100 + (m / 16) * 2;
    buf[0] = STX; buf[1] = CMD_READ;
    snprintf(reinterpret_cast<char*>(buf + 2), 5, "%04X", addr);
    buf[6] = '0'; buf[7] = '1';
    buf[8] = ETX;
    append_sum(buf, 1, 8);
    return 11;
}

static int build_read_s(unsigned char* buf, int s) {
    int addr = s / 8;
    buf[0] = STX; buf[1] = CMD_READ;
    snprintf(reinterpret_cast<char*>(buf + 2), 5, "%04X", addr);
    buf[6] = '0'; buf[7] = '1';
    buf[8] = ETX;
    append_sum(buf, 1, 8);
    return 11;
}

static int build_read_x_byte(unsigned char* buf, int x_octal) {
    int group = x_octal / 10;
    int addr = 0x0080 + group;
    buf[0] = STX; buf[1] = CMD_READ;
    snprintf(reinterpret_cast<char*>(buf + 2), 5, "%04X", addr);
    buf[6] = '0'; buf[7] = '1';
    buf[8] = ETX;
    append_sum(buf, 1, 8);
    return 11;
}

static int build_force_m(unsigned char* buf, int m, bool on) {
    int addr = 0x0800 + m;
    buf[0] = STX; buf[1] = on ? CMD_SET : CMD_RST;
    snprintf(reinterpret_cast<char*>(buf + 2), 5, "%04X", addr);
    // force M 没 LEN(直接 ETX)
    buf[6] = ETX;
    append_sum(buf, 1, 6);
    return 9;
}

static int build_force_x(unsigned char* buf, int x_octal, bool on) {
    int x_dec = (x_octal / 10) * 8 + (x_octal % 10);
    int addr = 0x0400 + x_dec;
    buf[0] = STX; buf[1] = on ? CMD_SET : CMD_RST;
    snprintf(reinterpret_cast<char*>(buf + 2), 5, "%04X", addr);
    buf[6] = ETX;
    append_sum(buf, 1, 6);
    return 9;
}

struct Conn {
    int fd = -1;
    bool open(const char* path) {
        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return false;
        sockaddr_un addr{}; addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
        if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            ::close(fd); fd = -1; return false;
        }
        return true;
    }
    void close() { if (fd >= 0) ::close(fd); fd = -1; }
};

// 发请求 + 收完整响应
//   读请求: 期望 STX 响应,固定读 data_len 字节
//   force 请求: 期望 1 字节 ACK/NAK
static bool send_recv_data(Conn& c, const unsigned char* req, int req_len,
                            unsigned char* resp, int resp_len) {
    if (write(c.fd, req, req_len) != req_len) return false;
    int got = 0;
    while (got < resp_len) {
        int n = read(c.fd, resp + got, resp_len - got);
        if (n <= 0) return false;
        got += n;
    }
    return true;
}

static bool send_recv_ack(Conn& c, const unsigned char* req, int req_len, bool& is_ack) {
    if (write(c.fd, req, req_len) != req_len) return false;
    unsigned char b = 0;
    int n = read(c.fd, &b, 1);
    if (n != 1) return false;
    is_ack = (b == ACK);
    return (b == ACK || b == NAK);
}

// ============ 业务测试 ============

static void basic_read(Conn& c) {
    printf("[basic] read D142/M1/S0\n");
    unsigned char req[16], resp[16];

    // D142
    int rl = build_read_d(req, 142);
    EXPECT(send_recv_data(c, req, rl, resp, 8), "readD D142 (8B data resp)");
    if (resp[0] == STX) {
        int lo = std::stoi(std::string(reinterpret_cast<char*>(resp + 1), 2), nullptr, 16);
        int hi = std::stoi(std::string(reinterpret_cast<char*>(resp + 3), 2), nullptr, 16);
        printf("  -> D142 = %d\n", hi * 256 + lo);
    }

    // M1 (M word 0)
    rl = build_read_m_word(req, 0);
    EXPECT(send_recv_data(c, req, rl, resp, 8), "readM M0..M15 word (8B data resp)");
    if (resp[0] == STX) {
        // 字节反转(对应 fx_plc_instance.cpp:543-554)
        int raw = std::stoi(std::string(reinterpret_cast<char*>(resp + 1), 4), nullptr, 16);
        int m_word = ((raw & 0x00FF) << 8) | ((raw & 0xFF00) >> 8);
        bool m1 = (m_word & 2) != 0;
        printf("  -> M0..M15 = 0x%04x  M1=%d\n", m_word, m1);
    }

    // S0 (字节读 6B 响应)
    rl = build_read_s(req, 0);
    EXPECT(send_recv_data(c, req, rl, resp, 6), "readS S0 (6B data resp)");
    if (resp[0] == STX) {
        int s_byte = std::stoi(std::string(reinterpret_cast<char*>(resp + 1), 2), nullptr, 16);
        printf("  -> S0..S7 = 0x%02x  S0=%d\n", s_byte, s_byte & 1);
    }
}

static void force_m(Conn& c) {
    printf("[forceM] M0=stop, M100=reset, M500=jog, M1023=test\n");
    int writes[4] = {0, 100, 500, 1023};
    for (int m : writes) {
        unsigned char req[16];
        bool ack = false;
        // ON
        int rl = build_force_m(req, m, true);
        EXPECT(send_recv_ack(c, req, rl, ack) && ack,
               ("forceM M" + std::to_string(m) + " ON -> ACK").c_str());
        // 读回
        rl = build_read_m_word(req, (m / 16) * 16);
        unsigned char resp[8];
        if (send_recv_data(c, req, rl, resp, 8) && resp[0] == STX) {
            int raw = std::stoi(std::string(reinterpret_cast<char*>(resp + 1), 4), nullptr, 16);
            int m_word = ((raw & 0x00FF) << 8) | ((raw & 0xFF00) >> 8);
            bool v = (m_word >> (m % 16)) & 1;
            EXPECT(v, ("readM M" + std::to_string(m) + " == ON").c_str());
        } else {
            g_fail++;
            printf("  [FAIL] readM M%d 读响应错\n", m);
        }
        // OFF
        rl = build_force_m(req, m, false);
        EXPECT(send_recv_ack(c, req, rl, ack) && ack,
               ("forceM M" + std::to_string(m) + " OFF -> ACK").c_str());
        // 读回
        rl = build_read_m_word(req, (m / 16) * 16);
        if (send_recv_data(c, req, rl, resp, 8) && resp[0] == STX) {
            int raw = std::stoi(std::string(reinterpret_cast<char*>(resp + 1), 4), nullptr, 16);
            int m_word = ((raw & 0x00FF) << 8) | ((raw & 0xFF00) >> 8);
            bool v = (m_word >> (m % 16)) & 1;
            EXPECT(!v, ("readM M" + std::to_string(m) + " == OFF").c_str());
        } else {
            g_fail++;
            printf("  [FAIL] readM M%d 读响应错\n", m);
        }
    }
}

static void fault_read(Conn& c) {
    printf("[fault] read X group 2/3/5 (X20, X32-X37, X50)\n");
    unsigned char req[16], resp[16];
    int groups[3] = {2, 3, 5};
    const char* labels[3] = {"readX group 2 (X20-27)", "readX group 3 (X30-37)", "readX group 5 (X50-57)"};
    for (int i = 0; i < 3; i++) {
        int rl = build_read_x_byte(req, groups[i] * 10);  // X20 = group 2
        EXPECT(send_recv_data(c, req, rl, resp, 6), labels[i]);
    }
    // 13 故障单点(force X 不可,只测读)
    int faults[13] = {020, 032, 033, 034, 035, 036, 037, 040, 041, 042, 043, 047, 052};
    for (int x : faults) {
        int rl = build_read_x_byte(req, x);
        EXPECT(send_recv_data(c, req, rl, resp, 6), ("readX X" + std::to_string(x)).c_str());
    }
}

static void force_x(Conn& c) {
    printf("[forceX] X45=start\n");
    unsigned char req[16], resp[16];
    bool ack = false;
    int rl = build_force_x(req, 45, true);
    EXPECT(send_recv_ack(c, req, rl, ack) && ack, "forceX X45 ON -> ACK");
    // 读回 X45
    rl = build_read_x_byte(req, 45);
    if (send_recv_data(c, req, rl, resp, 6) && resp[0] == STX) {
        int x_byte = std::stoi(std::string(reinterpret_cast<char*>(resp + 1), 2), nullptr, 16);
        bool v = (x_byte >> 5) & 1;  // X45 八进制 = 4*10+5,group=4,bit=5
        printf("  [debug] force ON readX byte=0x%02x bit5=%d\n", x_byte, (x_byte >> 5) & 1);
        EXPECT(v, "readX X45 == ON");
    } else {
        g_fail++;
        printf("  [FAIL] readX X45 读响应错\n");
    }
    // OFF
    rl = build_force_x(req, 45, false);
    EXPECT(send_recv_ack(c, req, rl, ack) && ack, "forceX X45 OFF -> ACK");
    rl = build_read_x_byte(req, 45);
    if (send_recv_data(c, req, rl, resp, 6) && resp[0] == STX) {
        int x_byte = std::stoi(std::string(reinterpret_cast<char*>(resp + 1), 2), nullptr, 16);
        bool v = (x_byte >> 5) & 1;
        EXPECT(!v, "readX X45 == OFF");
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("usage: %s <socketPath> <basic|fault|forceX|forceM|all>...\n", argv[0]);
        return 2;
    }
    const char* sock_path = argv[1];

    Conn c;
    if (!c.open(sock_path)) {
        printf("[FAIL] connect %s\n", sock_path);
        return 2;
    }
    printf("[OK] connected to %s\n", sock_path);

    for (int i = 2; i < argc; i++) {
        std::string s = argv[i];
        if (s == "basic" || s == "all") basic_read(c);
        if (s == "fault" || s == "all") fault_read(c);
        if (s == "forceX" || s == "all") force_x(c);
        if (s == "forceM" || s == "all") force_m(c);
    }
    c.close();
    printf("--- pass=%d fail=%d ---\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
