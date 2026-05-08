#include "data_layer/device/driver/device_driver.h"
#include "data_layer/device/driver/factory.h"
#include "common/log/log_manager.h"


#include <libavutil/error.h>
#include <cstring>

// 安全的 FFmpeg 错误信息获取（修复临时数组报错）
static std::string ffmpeg_error(int errnum) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(errnum, buf, sizeof(buf));
    return std::string(buf);
}



HikvisionNvrDriver::~HikvisionNvrDriver(){
    disconnect();
}
    
bool HikvisionNvrDriver::connect() {
    // if (connected_ || userId_ >= 0) {
    //     return true;
    // }

    // // 确保SDK环境已初始化
    // if (!sdkEnv_ || !sdkEnv_->isInitialized()) {
    //     return false;
    // }

    // // 登录参数
    // NET_DVR_USER_LOGIN_INFO loginInfo = { 0 };
    // NET_DVR_DEVICEINFO_V40 deviceInfo = { 0 };

    // // 赋值设备信息
    // strncpy(loginInfo.sDeviceAddress, node_.ip.c_str(),
    //         sizeof(loginInfo.sDeviceAddress) - 1);
    // loginInfo.wPort = static_cast<WORD>(node_.port);  // 海康默认8000
    // strncpy(loginInfo.sUserName, node_.username.c_str(),
    //         sizeof(loginInfo.sUserName) - 1);
    // strncpy(loginInfo.sPassword, node_.password.c_str(),
    //         sizeof(loginInfo.sPassword) - 1);

    // loginInfo.bUseAsynLogin = 0;  // 同步登录

    // // 调用海康SDK登录
    // userId_ = NET_DVR_Login_V40(&loginInfo, &deviceInfo);
    // if (userId_ < 0) {
    //     return false;
    // }

    // connected_ = true;
    return true;
}

bool HikvisionNvrDriver::disconnect() {
    // if (!connected_ || userId_ < 0) {
    //     return true;
    // }

    // // 海康登出
    // NET_DVR_Logout(userId_);
    // userId_ = -1;
    // connected_ = false;

    return true;
}
bool HikvisionNvrDriver::isConnected()   {
     return connected_; 
    }
std::string HikvisionNvrDriver::driverName() const { 

    return "HikvisionNvrDriver"; 
}

bool HikvisionNvrDriver::transact(const std::vector<uint8_t>& request,std::vector<uint8_t>& response,int timeout_ms){

}


bool HikvisionNvrDriver::fetchFrame(std::shared_ptr<FrameData>& outFrame) {
    return true;
}

RadarLqhDriver::~RadarLqhDriver(){

}

bool RadarLqhDriver::connect()  {
        
    return true;
}

bool RadarLqhDriver::disconnect()  { 
    return true; 
}

bool RadarLqhDriver::isConnected()  { 
    return connected_; 
}

std::string RadarLqhDriver::driverName() const  {
    return "RadarLqhDriver";
}

bool RadarLqhDriver::readPacket(std::vector<uint8_t>& data){
    return true;
}

bool RadarLqhDriver::fetchFrame(std::shared_ptr<FrameData>& outFrame) {
    return true;
}

bool  RadarLqhDriver::transact(const std::vector<uint8_t>& request,std::vector<uint8_t>& response,int timeout_ms){

}


ModbusRtuDriver::~ModbusRtuDriver(){
    disconnect();
}

bool ModbusRtuDriver::connect()  {
    if (connected_) return true;
    serial_fd_ = open(interface_.endpoint.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (serial_fd_ < 0) return false;

    if (!configureSerial(serial_fd_, interface_)) {
        close(serial_fd_);
        serial_fd_ = -1;
        return false;
    }

    connected_ = true;
    return true;
}

bool ModbusRtuDriver::disconnect()  { 
    if (serial_fd_ >= 0) {
        close(serial_fd_);
        serial_fd_ = -1;
    }
    connected_ = false;
    return true;
}

bool ModbusRtuDriver::isConnected()  { 
    return connected_;
}

std::string ModbusRtuDriver::driverName() const  {
    return "ModbusRtuDriver";
}

bool ModbusRtuDriver::fetchFrame(std::shared_ptr<FrameData>& outFrame) {
    return true;
}

bool ModbusRtuDriver::transact(const std::vector<uint8_t>& request,std::vector<uint8_t>& response,int timeout_ms)
{
    if (!connected_ && !connect()) return false;

    std::lock_guard<std::mutex> lock(io_mutex_);

    // ===== 1. send =====
    ssize_t sent = write(serial_fd_, request.data(), request.size());
    if (sent != (ssize_t)request.size()) return false;

    tcdrain(serial_fd_);

    // ===== 2. recv =====
    response.clear();

    auto start = std::chrono::steady_clock::now();
    auto last_recv = start;

    uint8_t buf[256];

    while (true) {

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(serial_fd_, &readfds);

        struct timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 20000; // 20ms polling

        int ret = select(serial_fd_ + 1, &readfds, nullptr, nullptr, &tv);

        if (ret > 0) {
            ssize_t len = read(serial_fd_, buf, sizeof(buf));

            if (len > 0) {
                response.insert(response.end(), buf, buf + len);
                last_recv = std::chrono::steady_clock::now();
            }
        }

        auto now = std::chrono::steady_clock::now();

        int total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                now - start)
                                .count();

        int idle_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                               now - last_recv)
                               .count();

        // ===== 1. 总超时 =====
        if (total_elapsed > timeout_ms) {
            break;
        }

        // ===== 2. 空闲超时（关键）=====
        if (!response.empty() && idle_elapsed > 50) {
            break;
        }
    }

    // ===== 3. 基本校验 =====
    if (response.size() < 5) return false;

    // ===== 4. CRC =====
    uint16_t crc_calc = modbus_crc16(response.data(), response.size() - 2);

    uint16_t crc_recv =
        (uint16_t)response[response.size() - 2] |
        ((uint16_t)response[response.size() - 1] << 8);

    if (crc_calc != crc_recv) return false;

    return true;
}

uint16_t ModbusRtuDriver::modbus_crc16(const uint8_t* data, size_t len){
     uint16_t crc = 0xFFFF;
    for (size_t pos = 0; pos < len; pos++) {
        crc ^= (uint16_t)data[pos];
        for (int i = 0; i < 8; i++) {
            if (crc & 0x0001) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

speed_t ModbusRtuDriver::baudToFlag(int baud){
    switch (baud) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        default: return B9600;
    }
}

bool ModbusRtuDriver::configureSerial(int fd, const InterfaceDefinition& iface){
    struct termios tty{};
    if (tcgetattr(fd, &tty) != 0) return false;

    cfsetospeed(&tty, baudToFlag(iface.serialPort.baud_rate));
    cfsetispeed(&tty, baudToFlag(iface.serialPort.baud_rate));

    // 8N1
    tty.c_cflag &= ~PARENB;
    if (iface.serialPort.parity == "even") tty.c_cflag |= PARENB;
    if (iface.serialPort.parity == "odd")  { tty.c_cflag |= PARENB; tty.c_cflag |= PARODD; }

    tty.c_cflag &= ~CSTOPB;
    if (iface.serialPort.stop_bits == 2) tty.c_cflag |= CSTOPB;

    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= (iface.serialPort.data_bits == 7 ? CS7 : CS8);

    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= CREAD | CLOCAL;

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    tty.c_oflag = 0;
    tty.c_lflag = 0;

    // 非阻塞 + 超时由 select 控制
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    return tcsetattr(fd, TCSANOW, &tty) == 0;
}


ModbusTcpDriver::~ModbusTcpDriver(){

}

bool ModbusTcpDriver::connect() {
        
        return true;
    }

bool ModbusTcpDriver::disconnect() {
    
    return true;
}

bool ModbusTcpDriver::isConnected() { 
    return connected_; 
}

std::string ModbusTcpDriver::driverName() const {
    return "ModbusTcpDriver";
}

int ModbusTcpDriver::readRegister(int addr){

}

bool ModbusTcpDriver::writeRegister(int addr, int value){

}
bool ModbusTcpDriver::fetchFrame(std::shared_ptr<FrameData>& outFrame) {
    return true;
}


bool  ModbusTcpDriver::transact(const std::vector<uint8_t>& request,std::vector<uint8_t>& response,int timeout_ms){
    
}

RtspFFmpegDriver::~RtspFFmpegDriver(){
     disconnect();
}   

#include <iostream>
// 如果你项目用别的日志，把 std::cerr 换成你的 LOG(ERROR)
using namespace std;

bool RtspFFmpegDriver::connect() {
    if (connected_) {
        return true;
    }

    if (!sdkEnv_ || !sdkEnv_->isInitialized()) {
        std::cerr << "[RTSP] SDK 环境未初始化" << std::endl;
        return false;
    }
    // 屏蔽 FFmpeg 多余日志（关键：去掉 hevc 解码报错刷屏）
    av_log_set_level(AV_LOG_QUIET);

    fmtCtx_ = avformat_alloc_context();
    if (!fmtCtx_) {
        std::cerr << "[RTSP] avformat_alloc_context 分配失败" << std::endl;
        return false;
    }

    std::string url = node_.definition.address;
    std::cerr << "[RTSP] 开始连接: " << url << std::endl;

    AVDictionary* options = nullptr;
    av_dict_set(&options, "rtsp_transport", "tcp", 0);
    av_dict_set(&options, "stimeout", "5000000", 0);
    av_dict_set(&options, "max_delay", "500000", 0);

    int ret = avformat_open_input(&fmtCtx_, url.c_str(), nullptr, &options);
    av_dict_free(&options);

    if (ret < 0) {
        std::cerr << "[RTSP] 打开流失败: " << ffmpeg_error(ret) << " (" << ret << ")" << std::endl;
        avformat_free_context(fmtCtx_);
        fmtCtx_ = nullptr;
        return false;
    }

    ret = avformat_find_stream_info(fmtCtx_, nullptr);
    if (ret < 0) {
        std::cerr << "[RTSP] 获取流信息失败: " << ffmpeg_error(ret) << " (" << ret << ")" << std::endl;
        avformat_close_input(&fmtCtx_);
        return false;
    }

    videoStreamIndex_ = av_find_best_stream(fmtCtx_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoStreamIndex_ < 0) {
        std::cerr << "[RTSP] 未找到视频流" << std::endl;
        avformat_close_input(&fmtCtx_);
        return false;
    }

    AVStream* stream = fmtCtx_->streams[videoStreamIndex_];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        std::cerr << "[RTSP] 找不到解码器，codec_id: " << stream->codecpar->codec_id << std::endl;
        avformat_close_input(&fmtCtx_);
        return false;
    }

    codecCtx_ = avcodec_alloc_context3(codec);
    if (!codecCtx_) {
        std::cerr << "[RTSP] 解码器上下文分配失败" << std::endl;
        avformat_close_input(&fmtCtx_);
        return false;
    }

    ret = avcodec_parameters_to_context(codecCtx_, stream->codecpar);
    if (ret < 0) {
        std::cerr << "[RTSP] 解码器参数复制失败: " << ffmpeg_error(ret) << std::endl;
        avcodec_free_context(&codecCtx_);
        avformat_close_input(&fmtCtx_);
        return false;
    }

    ret = avcodec_open2(codecCtx_, codec, nullptr);
    if (ret < 0) {
        std::cerr << "[RTSP] 打开解码器失败: " << ffmpeg_error(ret) << std::endl;
        avcodec_free_context(&codecCtx_);
        avformat_close_input(&fmtCtx_);
        return false;
    }

    packet_ = av_packet_alloc();
    frame_ = av_frame_alloc();
    if (!packet_ || !frame_) {
        std::cerr << "[RTSP] AVPacket/AVFrame 分配失败" << std::endl;
        releaseResources();
        return false;
    }

    std::cerr << "[RTSP] 连接成功，视频流索引: " << videoStreamIndex_ << std::endl;
    connected_ = true;
    return true;
}

bool RtspFFmpegDriver::readPacket(AVPacket* pkt) {
    av_packet_unref(pkt);

    while (true) {
        int ret = av_read_frame(fmtCtx_, pkt);
        if (ret < 0) {
            return false;
        }

        if (pkt->stream_index == videoStreamIndex_) {
            return true;
        }

        av_packet_unref(pkt);
    }
}


bool RtspFFmpegDriver::decodePacket(const AVPacket* packet, AVFrame* frame) {
    if (!codecCtx_ || !packet || !frame) {
        LOG_ERROR("Invalid pointer in decodePacket");
        return false;
    }

    int ret = avcodec_send_packet(codecCtx_, packet);
    if (ret < 0) {
        char err_buf[1024] = {0};
        av_strerror(ret, err_buf, sizeof(err_buf));
        LOG_ERROR("Send packet failed: " + std::string(err_buf));
        return false;
    }


    while (true) {
        ret = avcodec_receive_frame(codecCtx_, frame);
        
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            return false;
        }

        ret = avcodec_receive_frame(codecCtx_, frame);
        if (ret < 0) {
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                return false; 
            }
            char err_buf[1024] = {0};
            av_strerror(ret, err_buf, sizeof(err_buf));
            LOG_ERROR("Receive frame failed: " + std::string(err_buf));
            return false;
        }
    }    

    return true;
}


bool RtspFFmpegDriver::disconnect() {
    if (!connected_) return true;

    releaseResources();
    connected_ = false;
    return true;
}

bool RtspFFmpegDriver::isConnected()  { 
    return connected_; 
}


std::string RtspFFmpegDriver::driverName() const  {
    return "RtspFFmpegDriver";
}


bool RtspFFmpegDriver::fetchFrame(std::shared_ptr<FrameData>& outFrame) {
    if (!connected_ || !fmtCtx_ || !codecCtx_) {
        return false;
    }

    while (true) {

        // ===== 1. 读取一个 packet =====
        if (!readPacket(packet_)) {
            return false;
        }

        // ===== 2. 送入解码器 =====
        int ret = avcodec_send_packet(codecCtx_, packet_);
        av_packet_unref(packet_);

        if (ret < 0) {
            continue; // 丢掉错误包
        }

        // ===== 3. 循环取 frame（关键）=====
        while (true) {
            ret = avcodec_receive_frame(codecCtx_, frame_);

            if (ret == AVERROR(EAGAIN)) {
                break; // 需要更多 packet
            }

            if (ret == AVERROR_EOF) {
                return false;
            }

            if (ret < 0) {
                return false;
            }

            // ===== 4. 成功拿到一帧 =====

            AVFrame* cloned = av_frame_clone(frame_);

            auto deleter = [](AVFrame* f) {
                av_frame_free(&f);
            };

            outFrame = std::make_shared<FrameData>();
            outFrame->frame = std::shared_ptr<AVFrame>(cloned, deleter);
            outFrame->width = frame_->width;
            outFrame->height = frame_->height;
            outFrame->valid = true;

            // outFrame->isKeyFrame = frame_->key_frame;
            // outFrame->pts = frame_->pts;

            return true;
        }
    }
}


void RtspFFmpegDriver::releaseResources() {

    if (frame_) {
        av_frame_free(&frame_);
        frame_ = nullptr;
    }

    if (packet_) {
        av_packet_free(&packet_);
        packet_ = nullptr;
    }

    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
    }

    if (fmtCtx_) {
        avformat_close_input(&fmtCtx_);
        fmtCtx_ = nullptr;
    }

    videoStreamIndex_ = -1;
}

bool  RtspFFmpegDriver::transact(const std::vector<uint8_t>& request,std::vector<uint8_t>& response,int timeout_ms){
    
}

