#include <iostream>                       //可以跑485，"/dev/ttyS4"，可以跑usb转ttl，"/dev/ttyACM0"
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <cstring>
#include <iomanip>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <errno.h>

// 核心参数
const char* SERIAL_PORT = "/dev/ttyACM0";       //USB CDC ACM（标准协议）
const int BAUDRATE = B115200;
const int BUFFER_SIZE = 512;
const int POWER_ON_DELAY_MS = 150;
const int READ_TIMEOUT_MS = 4000;         // 超时400ms
const int MEASURE_INTERVAL_MS = 5000;    // 测量间隔5000ms

// 串口赋权
bool grantSerialPortPermission() {
    std::cout << "=== Step 0: Grant serial port permission ===" << std::endl;
    std::string chmod_cmd = "chmod 666 " + std::string(SERIAL_PORT);
    int ret = system(chmod_cmd.c_str());

    if (ret == -1) {
        std::cerr << "Error: Failed to execute chmod command" << std::endl;
        return false;
    } else if (WEXITSTATUS(ret) != 0) {
        std::cerr << "Warning: chmod failed (run program with sudo)" << std::endl;
        return false;
    }

    std::cout << "Success: Permission granted to " << SERIAL_PORT << std::endl;
    return true;
}

// 初始化串口
int initSerialPort() {
    int fd = open(SERIAL_PORT, O_RDWR | O_NOCTTY);
    if (fd == -1) {
        std::cerr << "Error: Failed to open " << SERIAL_PORT << std::endl;
        return -1;
    }

    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(fd, &tty) != 0) {
        std::cerr << "Error: Get serial attr failed" << std::endl;
        close(fd);
        return -1;
    }

    cfsetospeed(&tty, BAUDRATE);
    cfsetispeed(&tty, BAUDRATE);
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= CREAD | CLOCAL;

    tty.c_lflag = 0;
    tty.c_oflag = 0;
    tty.c_iflag = 0;

    tty.c_cc[VTIME] = 0;
    tty.c_cc[VMIN] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        std::cerr << "Error: Set serial attr failed" << std::endl;
        close(fd);
        return -1;
    }
    tcflush(fd, TCIOFLUSH);

    std::cout << "Success: Serial port initialized (115200 8N1)" << std::endl;
    return fd;
}

// 发送测量报文（带你要的打印格式）
int sendSingleMeasurePacket(int fd) {
    unsigned char measure_packet[] = {0xAA, 0x00, 0x00, 0x20, 0x00, 0x01, 0x00, 0x02, 0x23};   //一次快速距离测量
    // unsigned char measure_packet[] = {0xAA, 0x00, 0x00, 0x20, 0x00, 0x01, 0x00, 0x04, 0x25};   //自动连续距离测量，他根据设置的检测时间，接收的报文长度不定
    // unsigned char measure_packet[] = {0X58};   //自动连续距离测量
    std::cout << "\n[Sending Single Measure Packet] (9 bytes): ";
    for (int i = 0; i < sizeof(measure_packet); i++) {
        std::cout << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << (int)measure_packet[i] << " ";
    }
    std::cout << std::dec << std::endl;

    int bytes_written = write(fd, measure_packet, sizeof(measure_packet));
    if (bytes_written != sizeof(measure_packet)) {
        std::cerr << "Error: Send failed (" << bytes_written << "/9 bytes)" << std::endl;
        return -1;
    }
    return 0;
}

// 读取响应（严格按你要的格式打印）
int readResponse(int fd, unsigned char* buffer, int timeout_ms) {
    memset(buffer, 0, BUFFER_SIZE);
    int total_bytes = 0;
    auto start = std::chrono::steady_clock::now();

    while (true) {
        int bytes_read = read(fd, buffer + total_bytes, BUFFER_SIZE - total_bytes);
        if (bytes_read > 0) {
            total_bytes += bytes_read;
        }

        // 读到完整有效帧
        if (total_bytes >= 13 && buffer[0] == 0xAA && buffer[3] == 0x22 && buffer[5] == 0x03) {
            break;
        }

        // 超时判断
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed > timeout_ms) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }

    if (total_bytes <= 0) {
        std::cerr << "Error: Read timeout (" << timeout_ms << "ms)" << std::endl;
        return -1;
    }

    // 打印接收报文（完全和你要求一致）
    std::cout << "[Received Response] (" << total_bytes << " bytes): ";
    for (int i = 0; i < total_bytes; i++) {
        std::cout << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << (int)buffer[i] << " ";
    }
    std::cout << std::dec << std::endl;

    return total_bytes;
}

// 解析距离 + 校验和打印（完全你要的格式）
bool parseDistanceWithoutChecksum(const unsigned char* response, int length, int& distance_mm) {
    if (length < 13 || response[0] != 0xAA || response[3] != 0x22) {
        std::cerr << "Error: Invalid packet format (length=" << length << ")" << std::endl;
        return false;
    }

    distance_mm = (response[6] << 24) | (response[7] << 16) | (response[8] << 8) | response[9];
    if (distance_mm < 30 || distance_mm > 20000) {
        std::cerr << "Error: Distance out of range (" << distance_mm << "mm)" << std::endl;
        return false;
    }

    // 校验和计算（仅打印，不校验）
    unsigned char calc_checksum = 0;
    for (int i = 0; i < length - 1; i++) {
        calc_checksum += response[i];
    }

    // 你要的成功打印格式
    std::cout << "✅ Valid Distance: " << distance_mm << " mm (" << distance_mm / 1000.0 << " m) | Checksum: calc="
              << (int)calc_checksum << ", recv=" << (int)response[length - 1] << std::endl;
    return true;
}

int main() {
    if (!grantSerialPortPermission()) return 1;
    int serial_fd = initSerialPort();
    if (serial_fd == -1) return 1;

    std::cout << "\n=== Step 1: Sensor power-up delay (" << POWER_ON_DELAY_MS << "ms) ===" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(POWER_ON_DELAY_MS));

    std::cout << "\n=== Step 2: Baud rate matching ===" << std::endl;
    unsigned char baud_packet[] = {0x55};
    write(serial_fd, baud_packet, sizeof(baud_packet));
    std::cout << "Sent baud rate packet (0x55) | Single module ignore response" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::cout << "\n=== Step 3: Stable single measurement (" << MEASURE_INTERVAL_MS << "ms interval) ===" << std::endl;
    std::cout << "Info: Press Ctrl+C to stop | Target rate: " << 1000 / MEASURE_INTERVAL_MS << "Hz" << std::endl;

    unsigned char response_buffer[BUFFER_SIZE];
    int distance_mm = 0;

    while (true) {
        auto cycle_start = std::chrono::steady_clock::now();

        sendSingleMeasurePacket(serial_fd);
        int response_len = readResponse(serial_fd, response_buffer, READ_TIMEOUT_MS);

        if (response_len > 0) {
            parseDistanceWithoutChecksum(response_buffer, response_len, distance_mm);
        }

        // 固定周期
        auto cycle_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - cycle_start).count();
        if (cycle_elapsed < MEASURE_INTERVAL_MS) {
            std::this_thread::sleep_for(std::chrono::milliseconds(MEASURE_INTERVAL_MS - cycle_elapsed));
        }
    }

    close(serial_fd);
    return 0;
}

// #include <iostream>     //成功代码
// #include <fcntl.h>
// #include <termios.h>
// #include <unistd.h>
// #include <cstring>
// #include <iomanip>
// #include <chrono>
// #include <thread>
// #include <cstdlib>

// // 核心参数（放宽间隔，降低超时概率）
// const char* SERIAL_PORT = "/dev/ttyS4";
// const int BAUDRATE = B115200;
// const int BUFFER_SIZE = 512;
// const int POWER_ON_DELAY_MS = 150;        
// const int FIRST_MEASURE_TIMEOUT = 40;     
// const int NORMAL_MEASURE_TIMEOUT = 20;    
// const int MEASURE_INTERVAL_MS = 200;      // 放宽到200ms，减少超时

// // 串口赋权
// bool grantSerialPortPermission() {
//     std::cout << "=== Step 0: Grant serial port permission ===" << std::endl;
//     std::string chmod_cmd = "chmod 666 " + std::string(SERIAL_PORT);
//     int ret = system(chmod_cmd.c_str());
    
//     if (ret == -1) {
//         std::cerr << "Error: Failed to execute chmod command" << std::endl;
//         return false;
//     } else if (WEXITSTATUS(ret) != 0) {
//         std::cerr << "Warning: chmod failed (run program with sudo)" << std::endl;
//         return false;
//     }
    
//     std::cout << "Success: Permission granted to " << SERIAL_PORT << std::endl;
//     return true;
// }

// // 初始化串口
// int initSerialPort() {
//     int fd = open(SERIAL_PORT, O_RDWR | O_NOCTTY | O_NONBLOCK);
//     if (fd == -1) {
//         std::cerr << "Error: Failed to open " << SERIAL_PORT << std::endl;
//         return -1;
//     }

//     struct termios tty;
//     memset(&tty, 0, sizeof(tty));
//     if (tcgetattr(fd, &tty) != 0) {
//         std::cerr << "Error: Get serial attr failed" << std::endl;
//         close(fd);
//         return -1;
//     }

//     // 基础参数
//     cfsetospeed(&tty, BAUDRATE);
//     cfsetispeed(&tty, BAUDRATE);
//     tty.c_cflag &= ~PARENB;
//     tty.c_cflag &= ~CSTOPB;
//     tty.c_cflag &= ~CSIZE;
//     tty.c_cflag |= CS8;
//     tty.c_cflag &= ~CRTSCTS;
//     tty.c_cflag |= CREAD | CLOCAL;

//     tty.c_lflag = 0;
//     tty.c_oflag = 0;
//     tty.c_iflag = 0;

//     tty.c_cc[VTIME] = FIRST_MEASURE_TIMEOUT;
//     tty.c_cc[VMIN] = 12;

//     if (tcsetattr(fd, TCSANOW, &tty) != 0) {
//         std::cerr << "Error: Set serial attr failed" << std::endl;
//         close(fd);
//         return -1;
//     }
//     tcflush(fd, TCIOFLUSH);

//     std::cout << "Success: Serial port initialized (115200 8N1)" << std::endl;
//     return fd;
// }

// // 发送单次测量报文
// int sendSingleMeasurePacket(int fd) {
//     // unsigned char measure_packet[] = {0xAA, 0x00, 0x00, 0x20, 0x00, 0x01, 0x00, 0x02, 0x23};
//     unsigned char measure_packet[] = {0xAA, 0x7F, 0x00, 0x20, 0x00, 0x01, 0x00, 0x02, 0xA2};    //广播地址，但是无法回复
//     std::cout << "\n[Sending Single Measure Packet] (9 bytes): ";
//     for (int i = 0; i < sizeof(measure_packet); i++) {
//         std::cout << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << (int)measure_packet[i] << " ";
//     }
//     std::cout << std::dec << std::endl;

//     int bytes_written = write(fd, measure_packet, sizeof(measure_packet));
//     if (bytes_written != sizeof(measure_packet)) {
//         std::cerr << "Error: Send failed (" << bytes_written << "/9 bytes)" << std::endl;
//         return -1;
//     }

//     // RS485切换延迟
//     std::this_thread::sleep_for(std::chrono::milliseconds(300));
//     tcflush(fd, TCOFLUSH);
//     return 0;
// }

// // 读取响应
// int readResponse(int fd, unsigned char* buffer, int timeout_deci) {
//     struct termios tty;
//     tcgetattr(fd, &tty);
//     int old_timeout = tty.c_cc[VTIME];
//     tty.c_cc[VTIME] = timeout_deci;
//     tcsetattr(fd, TCSANOW, &tty);

//     memset(buffer, 0, BUFFER_SIZE);
//     int total_bytes = 0;
//     auto start = std::chrono::steady_clock::now();

//     while (total_bytes < BUFFER_SIZE) {
//         int bytes_read = read(fd, buffer + total_bytes, BUFFER_SIZE - total_bytes);   //读取的报文长度
//         if (bytes_read > 0) {
//             total_bytes += bytes_read;
//             if (total_bytes >= 12 && buffer[0] == 0xAA && buffer[3] == 0x22 && buffer[5] == 0x03) {
//                 break;
//             }else {
//                 std::cout << "Received invalid length message, length: " << total_bytes << ", data: ";
//                 for (int i = 0; i < total_bytes; i++) {
//                     printf("%02X ", static_cast<unsigned char>(buffer[i]));
//                 }
//                 std::cout << std::endl;
//                 break;
//             }

//         auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
//         if (elapsed > timeout_deci * 100) {
//             break;
//         }

//         std::this_thread::sleep_for(std::chrono::microseconds(200));
//     }

//     tty.c_cc[VTIME] = old_timeout;
//     tcsetattr(fd, TCSANOW, &tty);

//     if (total_bytes <= 0) {
//         std::cerr << "Error: Read timeout (" << timeout_deci * 100 << "ms)" << std::endl;
//         return -1;
//     }

//     std::cout << "[Received Response] (" << total_bytes << " bytes): ";     //打印正确参数格式的距离
//     for (int i = 0; i < total_bytes; i++) {
//         std::cout << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << (int)buffer[i] << " ";
//     }
//     std::cout << std::dec << std::endl;

//     return total_bytes;
//     }
// }

// // 核心修改：跳过校验和，仅校验格式+解析距离
// bool parseDistanceWithoutChecksum(const unsigned char* response, int length, int& distance_mm) {
//     // 仅校验基础格式
//     if (length < 12 || length >13 || response[0] != 0xAA || response[1] != 0x00 || response[3] != 0x22) {
//         std::cerr << "Error: Invalid packet format (length=" << length << ")" << std::endl;
//         return false;
//     }

//     // 直接解析距离（过滤无效值）
//     distance_mm = (response[6] << 24) | (response[7] << 16) | (response[8] << 8) | response[9];
//     if (distance_mm < 30 || distance_mm > 20000) {
//         std::cerr << "Error: Distance out of range (" << distance_mm << "mm)" << std::endl;
//         return false;
//     }

//     // 打印校验和信息（便于后续排查）
//     unsigned char calc_checksum = 0;
//     for (int i = 0; i < length-1; i++) {
//         calc_checksum += response[i];
//     }
//     std::cout << "✅ Valid Distance: " << distance_mm << " mm (" << distance_mm / 1000.0 << " m) | Checksum: calc=" << (int)calc_checksum << ", recv=" << (int)response[length-1] << std::endl;
//     return true;
// }

// int main() {
//     if (!grantSerialPortPermission()) {
//         return 1;
//     }

//     int serial_fd = initSerialPort();
//     if (serial_fd == -1) {
//         return 1;
//     }

//     std::cout << "\n=== Step 1: Sensor power-up delay (" << POWER_ON_DELAY_MS << "ms) ===" << std::endl;
//     std::this_thread::sleep_for(std::chrono::milliseconds(POWER_ON_DELAY_MS));

//     std::cout << "\n=== Step 2: Baud rate matching ===" << std::endl;
//     unsigned char baud_packet[] = {0x55};
//     write(serial_fd, baud_packet, sizeof(baud_packet));
//     std::cout << "Sent baud rate packet (0x55) | Single module ignore response" << std::endl;
//     std::this_thread::sleep_for(std::chrono::milliseconds(500));

//     std::cout << "\n=== Step 3: Stable single measurement (" << MEASURE_INTERVAL_MS << "ms interval) ===" << std::endl;
//     std::cout << "Info: Press Ctrl+C to stop | Target rate: " << 1000/MEASURE_INTERVAL_MS << "Hz" << std::endl;

//     unsigned char response_buffer[BUFFER_SIZE];
//     int distance_mm = 0;
//     bool first_measure = true;

//     while (true) {
//         auto cycle_start = std::chrono::steady_clock::now();

//         if (sendSingleMeasurePacket(serial_fd) == -1) {
//             std::this_thread::sleep_for(std::chrono::milliseconds(MEASURE_INTERVAL_MS / 2));
//             continue;
//         }

//         int timeout = first_measure ? FIRST_MEASURE_TIMEOUT : NORMAL_MEASURE_TIMEOUT;
//         int response_len = readResponse(serial_fd, response_buffer, timeout);
        
//         if (response_len > 0) {
//             if (parseDistanceWithoutChecksum(response_buffer, response_len, distance_mm)) {
//                 first_measure = false;
//             }
//         }

//         auto cycle_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - cycle_start).count();
//         if (cycle_elapsed < MEASURE_INTERVAL_MS) {
//             std::this_thread::sleep_for(std::chrono::milliseconds(MEASURE_INTERVAL_MS - cycle_elapsed));
//         }
//     }

//     close(serial_fd);
//     return 0;
// }
// ztl@RK356X:~/program/boxfiles/Sensor_a_Controller/laser_rangefinder$ sudo ./laser_rangefind
// === Step 0: Grant serial port permission ===
// Success: Permission granted to /dev/ttyS4
// Success: Serial port initialized (115200 8N1)

// === Step 1: Sensor power-up delay (150ms) ===

// === Step 2: Baud rate matching ===
// Sent baud rate packet (0x55) | Single module ignore response

// === Step 3: Stable single measurement (200ms interval) ===
// Info: Press Ctrl+C to stop | Target rate: 5Hz

// [Sending Single Measure Packet] (9 bytes): AA 00 00 20 00 01 00 02 23 
// [Received Response] (13 bytes): AA 00 00 22 00 03 00 00 00 B5 02 5E 3A 
// ✅ Valid Distance: 181 mm (0.181 m) | Checksum: calc=228, recv=58

// [Sending Single Measure Packet] (9 bytes): AA 00 00 20 00 01 00 02 23 
// Error: Read timeout (2000ms)

// [Sending Single Measure Packet] (9 bytes): AA 00 00 20 00 01 00 02 23 
// [Received Response] (13 bytes): AA 00 00 22 00 03 00 00 00 B4 02 5B 36 
// ✅ Valid Distance: 180 mm (0.18 m) | Checksum: calc=224, recv=54

// [Sending Single Measure Packet] (9 bytes): AA 00 00 20 00 01 00 02 23 
// Error: Read timeout (2000ms)

// [Sending Single Measure Packet] (9 bytes): AA 00 00 20 00 01 00 02 23 
// [Received Response] (13 bytes): AA 00 00 22 00 03 00 00 00 B4 02 59 34 
// ✅ Valid Distance: 180 mm (0.18 m) | Checksum: calc=222, recv=52

// [Sending Single Measure Packet] (9 bytes): AA 00 00 20 00 01 00 02 23 
// Error: Read timeout (2000ms)

// [Sending Single Measure Packet] (9 bytes): AA 00 00 20 00 01 00 02 23 
// [Received Response] (13 bytes): AA 00 00 22 00 03 00 00 00 D5 02 60 5C 
// ✅ Valid Distance: 213 mm (0.213 m) | Checksum: calc=6, recv=92

// [Sending Single Measure Packet] (9 bytes): AA 00 00 20 00 01 00 02 23 
// Error: Read timeout (2000ms)

// [Sending Single Measure Packet] (9 bytes): AA 00 00 20 00 01 00 02 23 
// [Received Response] (13 bytes): AA 00 00 22 00 03 00 00 00 D5 02 47 43 
// ✅ Valid Distance: 213 mm (0.213 m) | Checksum: calc=237, recv=67

// [Sending Single Measure Packet] (9 bytes): AA 00 00 20 00 01 00 02 23 
// Error: Read timeout (2000ms)

// [Sending Single Measure Packet] (9 bytes): AA 00 00 20 00 01 00 02 23 
// [Received Response] (13 bytes): AA 00 00 22 00 03 00 00 00 A9 02 7D 4D 
// ✅ Valid Distance: 169 mm (0.169 m) | Checksum: calc=247, recv=77





// 测距仪的检验和的计算函数

// #include <stdint.h>
// #include <iostream>
// #include <iomanip>  // 必须加，用于十六进制输出

// // 计算 JRT 传感器校验和
// uint8_t Get_Checksum(uint8_t data[8])
// {
//     uint8_t sum = 0;
//     // 从第2个字节开始加到第8个字节（索引1~7）
//     for(int i=1; i<8; i++)
//     {
//         sum += data[i];
//     }
//     return sum;
// }

// int main() {
//     // uint8_t cmd[8] = {0xAA, 0x51, 0x00, 0x20, 0x00, 0x01, 0x00, 0x00};
//     uint8_t cmd[8] = {0xAA, 0x80, 0x00, 0x0C};
//     uint8_t checksum = Get_Checksum(cmd); // 结果 = 0x72

//     // 输出十六进制（你要的 0x72）
//     std::cout << "校验和（十六进制）: 0x" << std::hex << std::uppercase << (int)checksum << std::endl;

//     // 输出十进制（方便对照）
//     std::cout << "校验和（十进制）: " << std::dec << (int)checksum << std::endl;

//     return 0;
// }