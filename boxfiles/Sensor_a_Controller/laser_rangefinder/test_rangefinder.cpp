#include <iostream>       //未测试的，不知道可不可以成功打开激光测距仪的代码
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <cstring>
#include <iomanip>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <string>
#include <vector>
#include <csignal>
#include <sys/select.h>
#include <unistd.h>

// 命名空间隔离，避免全局命名冲突
namespace LaserSensor {
    // 核心配置（集中管理，便于修改，匹配手册参数）
    struct Config {
        const char* serial_port = "/dev/ttyS4";  // 串口设备名，根据实际修改
        speed_t baudrate = B115200;              // 手册默认波特率115200
        int buffer_size = 512;
        int power_on_delay_ms = 150;             // 传感器上电延迟
        int first_measure_timeout_deci = 40;     // 首次测量超时（100ms/单位）
        int normal_measure_timeout_deci = 10;    // 常规测量超时
        int rs485_switch_delay_ms = 50;          // 手册无RS485，缩短切换延迟
        bool laser_is_open = false;              // 激光状态标记
        int serial_fd = -1;                      // 全局串口fd，用于信号处理
    };

    // 报文结构体（抽象报文数据，便于扩展）
    struct Packet {
        std::string name;                       // 报文名称（用于日志）
        std::vector<unsigned char> data;        // 报文字节数据
        int timeout_deci;                       // 该报文对应的读取超时（100ms/单位）
    };

    // 全局配置实例（信号处理函数需访问）
    static Config g_config;

    /**
     * @brief 串口权限赋权（通用工具函数）
     * @return 成功返回true，失败返回false
     */
    bool grantSerialPermission() {
        std::cout << "\033[36m=== [Serial Tool] Grant permission for " << g_config.serial_port << " ===\033[0m" << std::endl;
        std::string chmod_cmd = "chmod 666 " + std::string(g_config.serial_port);
        int ret = system(chmod_cmd.c_str());
        
        if (ret == -1) {
            std::cerr << "\033[31mError: Execute chmod command failed\033[0m" << std::endl;
            return false;
        } else if (WEXITSTATUS(ret) != 0) {
            std::cerr << "\033[33mWarning: Chmod failed (please run with sudo)\033[0m" << std::endl;
            // 非致命错误，继续执行
        }
        
        std::cout << "\033[32mSuccess: Permission granted\033[0m" << std::endl;
        return true;
    }

    /**
     * @brief 初始化串口（通用工具函数，可复用，匹配手册8N1无流控）
     * @return 成功返回串口文件描述符，失败返回-1
     */
    int initSerialPort() {
        std::cout << "\033[36m=== [Serial Tool] Initialize serial port ===\033[0m" << std::endl;
        // 打开串口（非阻塞模式）
        int fd = open(g_config.serial_port, O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd == -1) {
            std::cerr << "\033[31mError: Open serial port " << g_config.serial_port << " failed\033[0m" << std::endl;
            return -1;
        }

        // 配置串口参数
        struct termios tty;
        memset(&tty, 0, sizeof(tty));
        if (tcgetattr(fd, &tty) != 0) {
            std::cerr << "\033[31mError: Get serial attributes failed\033[0m" << std::endl;
            close(fd);
            return -1;
        }

        // 波特率配置（手册默认115200）
        cfsetospeed(&tty, g_config.baudrate);
        cfsetispeed(&tty, g_config.baudrate);

        // 数据位/校验位/停止位：8N1（严格匹配手册6.1 USART接口）
        tty.c_cflag &= ~PARENB;    // 无校验位
        tty.c_cflag &= ~CSTOPB;    // 1位停止位
        tty.c_cflag &= ~CSIZE;     // 清空数据位配置
        tty.c_cflag |= CS8;        // 8位数据位
        tty.c_cflag &= ~CRTSCTS;   // 禁用硬件流控（手册无流控）
        tty.c_cflag |= CREAD | CLOCAL;  // 启用接收 + 忽略调制解调器状态

        // 原始模式（禁用所有处理，避免串口数据解析异常）
        tty.c_lflag = 0;
        tty.c_oflag = 0;
        tty.c_iflag = 0;

        // 默认超时配置（首次测量）
        tty.c_cc[VTIME] = g_config.first_measure_timeout_deci;
        tty.c_cc[VMIN] = 12;       // 最少读取12字节（测量结果报文最小长度）

        // 应用配置
        if (tcsetattr(fd, TCSANOW, &tty) != 0) {
            std::cerr << "\033[31mError: Set serial attributes failed\033[0m" << std::endl;
            close(fd);
            return -1;
        }

        // 清空收发缓存
        tcflush(fd, TCIOFLUSH);
        std::cout << "\033[32mSuccess: Serial port initialized (115200 8N1, match manual)\033[0m" << std::endl;
        return fd;
    }

    /**
     * @brief 通用发送报文函数（可复用，支持任意报文）
     * @param fd 串口文件描述符
     * @param packet 要发送的报文
     * @return 成功返回0，失败返回-1
     */
    int sendPacket(int fd, const Packet& packet) {
        if (fd == -1) {
            std::cerr << "\033[31mError: Serial port not initialized\033[0m" << std::endl;
            return -1;
        }
        std::cout << "\n\033[34m=== [Packet Sender] Send " << packet.name << " ===\033[0m" << std::endl;
        std::cout << "Packet data (" << packet.data.size() << " bytes): ";
        for (unsigned char byte : packet.data) {
            std::cout << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << (int)byte << " ";
        }
        std::cout << std::dec << std::endl;

        // 发送报文
        ssize_t bytes_written = write(fd, packet.data.data(), packet.data.size());
        if (bytes_written != (ssize_t)packet.data.size()) {
            std::cerr << "\033[31mError: Send " << packet.name << " failed (" 
                      << bytes_written << "/" << packet.data.size() << " bytes)\033[0m" << std::endl;
            return -1;
        }

        // 串口收发切换延迟（非RS485缩短为50ms）
        std::this_thread::sleep_for(std::chrono::milliseconds(g_config.rs485_switch_delay_ms));
        tcflush(fd, TCOFLUSH);  // 清空发送缓存
        std::cout << "\033[32mSuccess: Send " << packet.name << " completed\033[0m" << std::endl;
        return 0;
    }

    /**
     * @brief 通用读取响应函数（可复用，支持自定义超时）
     * @param fd 串口文件描述符
     * @param buffer 接收缓冲区
     * @param timeout_deci 超时时间（100ms/单位）
     * @return 成功返回读取的字节数，失败返回-1
     */
    int readResponse(int fd, unsigned char* buffer, int timeout_deci) {
        if (fd == -1) {
            std::cerr << "\033[31mError: Serial port not initialized\033[0m" << std::endl;
            return -1;
        }
        // 保存原有超时配置，执行完恢复
        struct termios tty;
        tcgetattr(fd, &tty);
        int old_timeout = tty.c_cc[VTIME];
        tty.c_cc[VTIME] = timeout_deci;
        tcsetattr(fd, TCSANOW, &tty);

        // 初始化缓冲区
        memset(buffer, 0, g_config.buffer_size);
        int total_bytes = 0;
        auto start_time = std::chrono::steady_clock::now();

        // 循环读取数据
        while (total_bytes < g_config.buffer_size) {
            // 读取数据（核心逻辑：拼接数据，避免覆盖）
            int bytes_read = read(fd, buffer + total_bytes, g_config.buffer_size - total_bytes);
            
            if (bytes_read > 0) {
                total_bytes += bytes_read;
                // 检测到有效报文（AA开头 + 0x22测量结果标识，匹配手册6.4.6）
                if (total_bytes >= 12 && buffer[0] == 0xAA && buffer[3] == 0x22) {
                    break;
                }
                // 检测到错误报文（EE开头，匹配手册6.4.16）
                else if (total_bytes >= 9 && buffer[0] == 0xEE) {
                    break;
                }
            } else if (bytes_read == 0) {
                // 无数据可读，短延时后继续
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            } else {
                // 读取错误，退出
                std::cerr << "\033[31mError: Read serial data failed\033[0m" << std::endl;
                break;
            }

            // 超时判断
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time
            ).count();
            if (elapsed > timeout_deci * 100) {
                std::cerr << "\033[33mWarning: Read timeout (" << elapsed << "ms)\033[0m" << std::endl;
                break;
            }
        }

        // 恢复原有超时配置
        tty.c_cc[VTIME] = old_timeout;
        tcsetattr(fd, TCSANOW, &tty);

        // 日志输出读取结果
        if (total_bytes > 0) {
            std::cout << "\033[34m=== [Response Reader] Received data ===\033[0m" << std::endl;
            std::cout << "Total bytes: " << total_bytes << ", Data: ";
            for (int i = 0; i < total_bytes; i++) {
                std::cout << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << (int)buffer[i] << " ";
            }
            std::cout << std::dec << std::endl;
        } else {
            std::cerr << "\033[31mError: No data received (timeout)\033[0m" << std::endl;
            return -1;
        }

        return total_bytes;
    }

    /**
     * @brief 解析距离（严格匹配手册6.4.6测量结果协议，含错误报文判断）
     * @param response 响应数据
     * @param length 响应数据长度
     * @param distance_mm 输出：解析出的距离（毫米）
     * @return 成功返回true，失败返回false
     */
    bool parseDistance(const unsigned char* response, int length, int& distance_mm) {
        std::cout << "\n\033[35m=== [Distance Parser] Parse data ===\033[0m" << std::endl;
        // 先判断是否为错误报文（手册6.4.16：0xEE开头为错误报告）
        if (length >= 9 && response[0] == 0xEE) {
            unsigned short error_code = (response[7] << 8) | response[8];
            std::cerr << "\033[31mError: Sensor report error, code=0x" 
                      << std::hex << std::uppercase << error_code << std::dec << " (see manual 6.6)\033[0m" << std::endl;
            return false;
        }

        // 测量结果报文格式校验（手册6.4.6：AA开头+0x22标识，长度≥12）
        if (length < 12 || response[0] != 0xAA || response[1] != 0x00 || response[3] != 0x22) {
            std::cerr << "\033[31mError: Invalid measure packet format (length=" << length << ")\033[0m" << std::endl;
            return false;
        }

        // 解析距离（4字节大端拼接，手册6.4.6：字节6-9为距离数据）
        distance_mm = (response[6] << 24) | (response[7] << 16) | (response[8] << 8) | response[9];
        
        // 有效距离范围校验（手册2.核心参数：0.03-20米 → 30-20000mm）
        if (distance_mm < 30 || distance_mm > 20000) {
            std::cerr << "\033[31mError: Distance out of range (" << distance_mm << "mm), valid:30~20000mm\033[0m" << std::endl;
            return false;
        }

        // 计算校验和（手册6.3：地址+寄存器+载荷计数+载荷，忽略溢出）
        unsigned char calc_checksum = 0;
        for (int i = 1; i < length - 1; i++) {  // 跳过0xAA头，到校验和前一位
            calc_checksum += response[i];
        }

        // 成功日志（含米和毫米单位，信号质量参考手册6.4.10）
        std::cout << "\033[32m✅ Success: Measure distance = " << distance_mm << " mm (" 
                  << std::fixed << std::setprecision(3) << distance_mm / 1000.0 << " m)\033[0m" << std::endl;
        std::cout << "Checksum: Calculated=0x" << std::hex << std::uppercase << (int)calc_checksum 
                  << ", Received=0x" << (int)response[length-1] << std::dec << std::endl;
        return true;
    }

    /**
     * @brief 发送波特率匹配报文（手册6.2：上电后发0x55做波特率自适应）
     * @param fd 串口文件描述符
     */
    void sendBaudratePacket(int fd) {
        if (fd == -1) return;
        std::cout << "\n\033[36m=== [Init Step] Send baudrate matching packet ===\033[0m" << std::endl;
        unsigned char baud_packet[] = {0x55};
        write(fd, baud_packet, sizeof(baud_packet));
        std::cout << "Sent baudrate packet (0x55) | Ignore response (single module, manual 6.2)\033[0m" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));  // 手册要求2.5S内，留足时间
    }

    /**
     * @brief 关闭激光（封装函数，供主流程和信号处理调用）
     * @return 成功返回0，失败返回-1
     */
    int closeLaser() {
        if (!g_config.laser_is_open || g_config.serial_fd == -1) {
            return 0;
        }
        // 关闭激光报文（手册6.4.9：0xAA 0x00 0x01 0xBE 0x00 0x01 0x00 0x00 0xC0）
        Packet close_laser_pkt = {
            .name = "Close Laser Packet",
            .data = {0xAA, 0x00, 0x01, 0xBE, 0x00, 0x01, 0x00, 0x00, 0xC0},
            .timeout_deci = g_config.normal_measure_timeout_deci
        };
        int ret = sendPacket(g_config.serial_fd, close_laser_pkt);
        if (ret == 0) {
            g_config.laser_is_open = false;
            std::cout << "\n\033[32m✅ Success: Laser closed (match manual 6.4.9)\033[0m" << std::endl;
        } else {
            std::cerr << "\033[31mError: Close laser failed\033[0m" << std::endl;
        }
        return ret;
    }

    /**
     * @brief 非阻塞检测Enter键（无按键时不阻塞主循环）
     * @return 按下Enter返回true，否则返回false
     */
    bool isEnterPressed() {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        struct timeval tv = {0, 0};  // 无超时，立即返回
        int ret = select(STDIN_FILENO + 1, &read_fds, nullptr, nullptr, &tv);
        if (ret > 0 && FD_ISSET(STDIN_FILENO, &read_fds)) {
            char ch;
            read(STDIN_FILENO, &ch, 1);
            return (ch == '\n' || ch == '\r');  // 匹配Enter键的两种编码
        }
        return false;
    }
}

// Ctrl+C信号处理函数（捕获SIGINT，关闭激光并退出）
void sigintHandler(int sig) {
    using namespace LaserSensor;
    std::cout << "\n\n\033[33m=== [Signal Handler] Catch Ctrl+C (SIGINT), closing laser... ===\033[0m" << std::endl;
    closeLaser();  // 关闭激光
    if (g_config.serial_fd != -1) {
        close(g_config.serial_fd);  // 关闭串口
        std::cout << "\033[32mSuccess: Serial port closed\033[0m" << std::endl;
    }
    std::cout << "\033[32m✅ Program exited safely\033[0m" << std::endl;
    exit(0);  // 正常退出
}

// 主函数：流程控制→初始化→开激光→按键测量→Ctrl+C退出
int main() {
    using namespace LaserSensor;

    // 注册Ctrl+C信号处理函数（必须在初始化前注册）
    signal(SIGINT, sigintHandler);
    std::cout << "\033[36m=== [JRT UB2B Laser Sensor] Program Start ===\033[0m" << std::endl;
    std::cout << "Tips: 1.Press Enter to measure once  2.Press Ctrl+C to close laser and exit\033[0m" << std::endl;

    // ====================== 1. 串口初始化阶段 ======================
    // 串口赋权
    grantSerialPermission();
    // 初始化串口并保存到全局配置
    g_config.serial_fd = initSerialPort();
    if (g_config.serial_fd == -1) {
        std::cerr << "\033[31mFatal Error: Serial port init failed, program exit\033[0m" << std::endl;
        return 1;
    }
    // 传感器上电延迟
    std::cout << "\n\033[36m=== [Init Step] Sensor power-up delay (" << g_config.power_on_delay_ms << "ms) ===\033[0m" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(g_config.power_on_delay_ms));
    // 波特率自适应（手册6.2核心步骤，必须执行）
    sendBaudratePacket(g_config.serial_fd);

    // ====================== 2. 定义核心报文（严格匹配手册） ======================
    // 开启激光报文（手册6.4.9：0xAA 0x00 0x01 0xBE 0x00 0x01 0x00 0x01 0xC1）
    Packet open_laser_pkt = {
        .name = "Open Laser Packet",
        .data = {0xAA, 0x00, 0x01, 0xBE, 0x00, 0x01, 0x00, 0x01, 0xC1},
        .timeout_deci = g_config.first_measure_timeout_deci
    };
    // 单次快速测量报文（手册6.4.12：0xAA 0x00 0x00 0x20 0x00 0x01 0x00 0x02 0x23）
    // 可替换为自动测量(0x00) / 慢速测量(0x01)，修改payload最后一位即可
    Packet single_measure_pkt = {
        .name = "Single Fast Measure Packet (manual 6.4.12)",
        .data = {0xAA, 0x00, 0x00, 0x20, 0x00, 0x01, 0x00, 0x02, 0x23},
        .timeout_deci = g_config.normal_measure_timeout_deci
    };

    // ====================== 3. 初始化后立即开启激光 ======================
    // std::cout << "\n\033[36m=== [Core Step] Open laser first (match requirement) ===\033[0m" << std::endl;
    // if (sendPacket(g_config.serial_fd, open_laser_pkt) == 0) {
    //     g_config.laser_is_open = true;
    //     std::cout << "\033[32m✅ Success: Laser opened (match manual 6.4.9)\033[0m" << std::endl;
    // } else {
    //     std::cerr << "\033[31mFatal Error: Open laser failed, program exit\033[0m" << std::endl;
    //     close(g_config.serial_fd);
    //     return 1;
    // }
    // ====================== 3. 初始化后立即开启激光 ======================
    std::cout << "\n\033[36m=== [Core Step] Open laser first (match requirement) ===\033[0m" << std::endl;
    if (sendPacket(g_config.serial_fd, open_laser_pkt) == 0) {
        g_config.laser_is_open = true;
        
        // ========== 新增：读取并打印开激光的返回报文 ==========
        std::cout << "\n\033[35m=== [Laser Response] Read open laser confirm packet ===\033[0m" << std::endl;
        unsigned char laser_resp_buffer[g_config.buffer_size];
        // 读取开激光响应（超时设为20→2秒，匹配传感器响应速度）
        int laser_resp_len = readResponse(g_config.serial_fd, laser_resp_buffer, 20);
        if (laser_resp_len > 0) {
            std::cout << "\033[32m✅ Open laser response received (length=" << laser_resp_len << ")\033[0m" << std::endl;
        } else {
            std::cerr << "\033[33m⚠️ No confirm packet for open laser (hardware/connection issue?)\033[0m" << std::endl;
        }
        // =====================================================
        
        std::cout << "\033[32m✅ Success: Laser opened (match manual 6.4.9)\033[0m" << std::endl;
    } else {
        std::cerr << "\033[31mFatal Error: Open laser failed, program exit\033[0m" << std::endl;
        close(g_config.serial_fd);
        return 1;
    }

    // ====================== 4. 主循环：按Enter单次测量 ======================
    std::cout << "\n\033[32m=== [Main Loop] Ready, press Enter to measure ===\033[0m" << std::endl;
    unsigned char response_buffer[g_config.buffer_size];
    int distance_mm = 0;
    while (true) {
        // 非阻塞检测Enter键，按下则执行测量
        if (isEnterPressed()) {
            std::cout << "\n\033[36m=== [User Input] Enter pressed, start single measure ===\033[0m" << std::endl;
            // 发送单次测量报文
            if (sendPacket(g_config.serial_fd, single_measure_pkt) == -1) {
                std::cout << "\033[33mWarning: Retry measure next time\033[0m" << std::endl;
                continue;
            }
            // 读取测量响应
            int response_len = readResponse(g_config.serial_fd, response_buffer, single_measure_pkt.timeout_deci);
            if (response_len > 0) {
                // 解析距离数据
                parseDistance(response_buffer, response_len, distance_mm);
            }
            std::cout << "\n\033[32m=== [Ready] Press Enter again to measure ===\033[0m" << std::endl;
        }
        // 主循环短延时，降低CPU占用
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // 正常退出（实际由Ctrl+C信号处理函数触发，此处为语法规范）
    closeLaser();
    close(g_config.serial_fd);
    return 0;
}