#include <iostream>     //可以跑通的电机控制器代码
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <cstring>
#include <string>
#include <sstream>
#include <iomanip>
#include <cerrno>
#include <sys/stat.h>
#include <dirent.h>
#include <chrono>

// 日志宏定义（模拟学长的logger.h）
#define LOG_INFO(msg) std::cout << "[INFO] " << msg << std::endl
#define LOG_ERROR(msg) std::cerr << "[ERROR] " << msg << std::endl

// 电机状态结构体（对应学长的MotorStatus）
struct MotorStatus {
    uint16_t statusByte;  // 状态字节（前两个字节组合）
    int16_t motor1;       // 电机1数值
    int16_t motor2;       // 电机2数值
};

// 核心控制类（借鉴学长的CarControlDriver）
class CarControlDriver {
public:
    CarControlDriver() : serial_fd_(-1), gpio_num_(46), gpio_value_(0) {}
    ~CarControlDriver() { 
        closeSerial(); 
    }

    // 初始化：包含GPIO和串口
    bool init(const std::string& port = "/dev/ttyS4", int baud = 9600) {
        // 1. 初始化485 GPIO
        if (!init485GPIO(gpio_num_, gpio_value_)) {
            LOG_ERROR("CarControl: GPIO" + std::to_string(gpio_num_) + " init failed");
            return false;
        }

        // 2. 初始化串口（完全复用学长的串口配置逻辑）
        port_ = port;
        serial_fd_ = open(port.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
        if (serial_fd_ < 0) {
            LOG_ERROR("CarControl: open serial failed: " + std::string(strerror(errno)));
            return false;
        }

        struct termios tty;
        memset(&tty, 0, sizeof(tty));
        if (tcgetattr(serial_fd_, &tty) != 0) {
            LOG_ERROR("CarControl: tcgetattr failed: " + std::string(strerror(errno)));
            closeSerial();
            return false;
        }

        // 波特率配置（支持多波特率）
        speed_t speed = B9600;
        switch (baud) {
            case 9600: speed = B9600; break;
            case 19200: speed = B19200; break;
            case 38400: speed = B38400; break;
            case 115200: speed = B115200; break;
            default: speed = B9600; break;
        }
        cfsetospeed(&tty, speed);
        cfsetispeed(&tty, speed);

        // 8N1配置（和学长保持一致）
        tty.c_cflag &= ~CSIZE;
        tty.c_cflag |= CS8;
        tty.c_cflag &= ~PARENB;
        tty.c_cflag &= ~CSTOPB;
        tty.c_cflag &= ~CRTSCTS;
        tty.c_cflag |= CLOCAL | CREAD;

        tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
        tty.c_iflag &= ~(IXON | IXOFF | IXANY);
        tty.c_oflag &= ~OPOST;

        // 非阻塞读取（关键：和学长保持一致的超时配置）
        tty.c_cc[VTIME] = 1;  // 100ms超时
        tty.c_cc[VMIN] = 0;

        if (tcsetattr(serial_fd_, TCSANOW, &tty) != 0) {
            LOG_ERROR("CarControl: tcsetattr failed: " + std::string(strerror(errno)));
            closeSerial();
            return false;
        }

        fcntl(serial_fd_, F_SETFL, 0);
        LOG_INFO("CarControl: serial init success - port: " + port + ", baud: " + std::to_string(baud));
        return true;
    }

    // 关闭串口
    void closeSerial() {
        if (serial_fd_ >= 0) {
            close(serial_fd_);
            serial_fd_ = -1;
            LOG_INFO("CarControl: serial closed");
        }
    }

    // 发送控制帧（复用学长的帧构建逻辑）
    bool sendControl(int16_t motor1, int16_t motor2) {
        if (serial_fd_ < 0) {
            LOG_ERROR("CarControl: serial not initialized");
            return false;
        }

        // 构建8字节控制帧（和学长完全一致）
        uint8_t frame[8];
        frame[0] = 0x55;          // 帧头1
        frame[1] = 0x5A;          // 帧头2
        frame[2] = 0x05;          // 功能码
        uint16_t v1 = *reinterpret_cast<uint16_t*>(&motor1);
        uint16_t v2 = *reinterpret_cast<uint16_t*>(&motor2);
        frame[3] = (v1 >> 8) & 0xFF;  // motor1高字节
        frame[4] = v1 & 0xFF;         // motor1低字节
        frame[5] = (v2 >> 8) & 0xFF;  // motor2高字节
        frame[6] = v2 & 0xFF;         // motor2低字节
        frame[7] = 0x00;              // 保留位

        // 打印发送帧（和学长的日志格式一致）
        std::string hexStr = bytesToHex(frame, sizeof(frame));
        LOG_INFO("CarControl: Sending frame - Motor1: " + std::to_string(motor1) + 
                 ", Motor2: " + std::to_string(motor2) + ", Frame: " + hexStr);

        // 发送数据（复用学长的发送逻辑）
        ssize_t n = write(serial_fd_, frame, sizeof(frame));
        if (n != (ssize_t)sizeof(frame)) {
            LOG_ERROR("CarControl: write failed: " + std::string(strerror(errno)));
            return false;
        }
        tcdrain(serial_fd_);  // 等待发送完成
        return true;
    }

    // 读取状态（完全复用学长的解析逻辑）
    bool readStatus(MotorStatus& out) {
        if (serial_fd_ < 0) {
            LOG_ERROR("CarControl: serial not initialized");
            return false;
        }

        uint8_t buf[8];
        memset(buf, 0, sizeof(buf));
        ssize_t n = read(serial_fd_, buf, sizeof(buf));

        // 错误处理（和学长一致）
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return false;  // 超时无数据
            }
            LOG_ERROR("CarControl: read error: " + std::string(strerror(errno)));
            return false;
        }

        // 数据长度检查
        if (n < 2) {
            return false;  // 至少2字节才有意义
        }

        // 过滤全0帧（学长的关键优化）
        bool allZero = true;
        for (ssize_t i = 0; i < n; ++i) {
            if (buf[i] != 0) {
                allZero = false;
                break;
            }
        }
        if (allZero) {
            LOG_INFO("CarControl: all-zero frame received (device not connected)");
            return false;
        }

        // 打印接收帧
        std::string hexStr = bytesToHex(buf, n);
        LOG_INFO("CarControl: Received frame: " + hexStr + " (" + std::to_string(n) + " bytes)");

        // 解析状态（和学长一致的解析逻辑）
        out.statusByte = (static_cast<uint16_t>(buf[0]) << 8) | static_cast<uint16_t>(buf[1]);
        out.motor1 = 0;
        out.motor2 = 0;

        // 如果有更多字节，可以扩展解析逻辑
        if (n >= 6) {
            out.motor1 = to_signed16(make_u16(buf[3], buf[4]));
            out.motor2 = to_signed16(make_u16(buf[5], buf[6]));
        }

        return true;
    }

    // 自动收发循环（发送+接收一体化）
    void autoControlLoop(int16_t motor1, int16_t motor2, int interval_ms = 200) {
        LOG_INFO("CarControl: start auto control loop (interval: " + std::to_string(interval_ms) + "ms)");
        MotorStatus status;

        while (true) {
            // 1. 发送控制帧
            if (sendControl(motor1, motor2)) {
                // 2. 短暂延迟后读取状态
                usleep(60000);  // 和你原代码的SEND_DELAY一致
                if (readStatus(status)) {
                    // 打印解析后的状态
                    LOG_INFO("CarControl: Status - statusByte: 0x" + 
                             bytesToHex(reinterpret_cast<uint8_t*>(&status.statusByte), 2) +
                             ", Motor1: " + std::to_string(status.motor1) +
                             ", Motor2: " + std::to_string(status.motor2));
                } else {
                    LOG_INFO("CarControl: no valid status received");
                }
            } else {
                LOG_ERROR("CarControl: send control frame failed");
            }

            // 轮询间隔
            usleep(interval_ms * 1000);
        }
    }

private:
    // GPIO初始化（适配485控制）
    bool init485GPIO(int gpio_num, int value) {
        std::string export_path = "/sys/class/gpio/export";
        std::string gpio_base = "/sys/class/gpio/gpio" + std::to_string(gpio_num);
        std::string dir_path = gpio_base + "/direction";
        std::string val_path = gpio_base + "/value";

        // 导出GPIO
        DIR* dir = opendir(gpio_base.c_str());
        if (dir == NULL) {
            int fd = open(export_path.c_str(), O_WRONLY);
            if (fd < 0 && errno != EBUSY) {
                return false;
            }
            if (fd >= 0) {
                write(fd, std::to_string(gpio_num).c_str(), std::to_string(gpio_num).length());
                close(fd);
                usleep(100000);
            }
        } else {
            closedir(dir);
        }

        // 设置为输出
        int fd = open(dir_path.c_str(), O_WRONLY);
        if (fd < 0) return false;
        write(fd, "out", 3);
        close(fd);

        // 设置初始值
        fd = open(val_path.c_str(), O_WRONLY);
        if (fd < 0) return false;
        write(fd, std::to_string(value).c_str(), 1);
        close(fd);

        return true;
    }

    // 字节转16进制字符串（完全复用学长的实现）
    std::string bytesToHex(const uint8_t* bytes, size_t len) {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (size_t i = 0; i < len; ++i) {
            if (i > 0) ss << " ";
            ss << std::setw(2) << static_cast<int>(bytes[i]);
        }
        return ss.str();
    }

    // 辅助函数（复用学长的工具函数）
    static inline uint16_t make_u16(uint8_t hi, uint8_t lo) {
        return (static_cast<uint16_t>(hi) << 8) | lo;
    }

    static inline int16_t to_signed16(uint16_t v) {
        return *reinterpret_cast<int16_t*>(&v);
    }

    // 成员变量
    int serial_fd_;          // 串口文件描述符
    std::string port_;       // 串口路径
    int gpio_num_;           // 485 GPIO编号
    int gpio_value_;         // GPIO初始值
};

// 主函数：演示使用
int main() {
    CarControlDriver driver;

    // 1. 初始化（串口+GPIO）
    if (!driver.init("/dev/ttyS4", 9600)) {
        LOG_ERROR("CarControl: init failed");
        return -1;
    }

    // // 2. 启动自动控制循环（发送指定电机值，持续读取状态）
    // // 示例：motor1=44, motor2=44（对应你之前的0x01 0x2C）
    // try {
    //     driver.autoControlLoop(44, 44, 200);
    // } catch (...) {
    //     LOG_INFO("CarControl: loop stopped by user");
    // }

    //可以定义运行时间的
    // 1. 定义运行时长（毫秒）：3秒 = 3000ms
    const int RUN_DURATION_MS = 3000;
    // 2. 记录开始时间
    auto start_time = std::chrono::steady_clock::now();
    // 3. 定义运动参数（前进：motor1=44, motor2=44）
    int16_t motor1 = 44, motor2 = 44;
    MotorStatus status;

    LOG_INFO("CarControl: start moving forward for 3 seconds...");
    while (true) {
        // 计算已运行时间
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
        
        // 达到时长后停止
        if (duration >= RUN_DURATION_MS) {
            motor1 = 0;
            motor2 = 0;
            // 发送停止指令
            driver.sendControl(motor1, motor2);
            LOG_INFO("CarControl: stop moving (run for " + std::to_string(duration) + "ms)");
            break;
        }

        // 发送运动指令
        if (driver.sendControl(motor1, motor2)) {
            usleep(60000);
            driver.readStatus(status);
        }
        // 循环间隔（200ms）
        usleep(200 * 1000);
    }


    return 0;
}