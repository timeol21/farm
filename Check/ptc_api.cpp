#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <cstring>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

//  g++ -std=c++17 ptc_api.cpp -o ptc_api -lavformat -lavcodec -lavutil

//==================== 全局串口句柄与设备地址 ====================
static int g_serial_fd = -1;
const uint8_t CAM_DEFAULT_ADDR = 0x00;   // 设备出厂序号0

/**
 * @brief 设置本地系统串口fd波特率(程序侧，不是发指令给摄像头)
 */
static bool serial_set_local_baud(int fd, int baud_bsp)
{
    struct termios opt;
    if(tcgetattr(fd, &opt) !=0) return false;
    speed_t speed;
    switch (baud_bsp)
    {
        case 9600:    speed = B9600; break;
        case 115200:  speed = B115200; break;
        default:
            std::cerr << "本地串口仅支持9600/115200\n";
            return false;
    }
    cfsetispeed(&opt, speed);
    cfsetospeed(&opt, speed);
    return tcsetattr(fd, TCSANOW, &opt) == 0;
}

/**
 * @brief 打开RS485串口 8N1
 * @param dev 设备节点 /dev/ttyUSB0
 * @param baud 初始波特率，PTC5M0A RS485只能9600/115200
 * @return fd >=0成功
 */
int ptc_serial_open(const char* dev, int baud)
{
    g_serial_fd = open(dev, O_RDWR | O_NOCTTY);
    if(g_serial_fd <0)
    {
        std::cerr << "ptc_serial_open failed dev=" << dev << "\n";
        return -1;
    }
    struct termios opt;
    tcgetattr(g_serial_fd, &opt);
    opt.c_cflag &= ~CSIZE;
    opt.c_cflag |= CS8;
    opt.c_cflag &= ~CSTOPB;
    opt.c_cflag &= ~PARENB;
    opt.c_cflag &= ~CRTSCTS;
    opt.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    opt.c_iflag &= ~(IXON | IXOFF | IXANY);
    opt.c_oflag &= ~OPOST;

    if(!serial_set_local_baud(g_serial_fd, baud))
    {
        close(g_serial_fd);
        g_serial_fd = -1;
        return -1;
    }
    tcflush(g_serial_fd, TCIOFLUSH);
    return g_serial_fd;
}

/**
 * @brief 关闭串口
 */
void ptc_serial_close()
{
    if(g_serial_fd >=0)
    {
        close(g_serial_fd);
        g_serial_fd = -1;
    }
}

/**
 * @brief 底层一问一答收发接口
 * @param tx 发送指令字节数组
 * @param tx_len 发送长度
 * @param rx 接收缓冲区
 * @param rx_max 接收缓冲区最大字节
 * @param timeout_ms 超时毫秒
 * @return >0读到字节；<=0超时错误
 */
int ptc_raw_transfer(const uint8_t* tx, int tx_len, uint8_t* rx, int rx_max, int timeout_ms)
{
    if(g_serial_fd <0) return -2;
    tcflush(g_serial_fd, TCIOFLUSH);
    write(g_serial_fd, tx, tx_len);

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(g_serial_fd, &rfds);
    struct timeval tv;
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms %1000)*1000;

    int ret = select(g_serial_fd+1, &rfds, nullptr, nullptr, &tv);
    if(ret <= 0) return -1;
    return read(g_serial_fd, rx, rx_max);
}

//==================== 业务指令接口 ====================

/**
 * @brief 1 查询版本指令 56 XX 11 00
 * @param cam_addr 设备序号
 * @param out_version 输出版本字符串
 * @return true成功
 */
bool ptc_query_version(uint8_t cam_addr, std::string& out_version)
{
    uint8_t cmd[] = {0x56, cam_addr, 0x11, 0x00};
    uint8_t buf[128];
    int n = ptc_raw_transfer(cmd, sizeof(cmd), buf, sizeof(buf), 1200);
    if(n <5) return false;
    //应答:76 addr 11 00 len str...
    if(buf[0] !=0x76 || buf[2] != 0x11) return false;
    uint8_t str_len = buf[4];
    if((5+str_len) > (size_t)n) return false;
    out_version.assign((char*)(buf+5), str_len);
    return true;
}

/**
 * @brief 2 复位指令 56 XX 26 00
 * @param cam_addr 设备序号
 * @return true发送应答成功；复位后硬件需要等待3.5s初始化
 */
bool ptc_reset(uint8_t cam_addr)
{
    uint8_t cmd[] = {0x56, cam_addr, 0x26, 0x00};
    uint8_t buf[32];
    int n = ptc_raw_transfer(cmd, sizeof(cmd), buf, sizeof(buf),1200);
    if(n >=4 && buf[0]==0x76 && buf[2]==0x26)
    {
        return true;
    }
    return false;
}

/**
 * @brief 3 JPG单张拍照指令 56 XX 36 01 00
 * @param cam_addr 设备序号
 * @return true拍照命令应答ok；图片存入0号缓存
 */
bool ptc_take_photo_jpg(uint8_t cam_addr)
{
    uint8_t cmd[] = {0x56, cam_addr, 0x36, 0x01, 0x00};
    uint8_t buf[32];
    int n = ptc_raw_transfer(cmd, sizeof(cmd), buf, sizeof(buf),1500);
    if(n>=5 && buf[0]==0x76 && buf[2]==0x36)
    {
        return true;
    }
    return false;
}

/**
 * @brief 4 读取图片长度指令 56 XX 34 01 00  读0号缓存
 * @param cam_addr 设备序号
 * @return >0图片字节大小，<=0失败
 */
int ptc_get_jpg_length(uint8_t cam_addr)
{
    uint8_t cmd[] = {0x56, cam_addr, 0x34, 0x01, 0x00};
    uint8_t buf[64];
    int n = ptc_raw_transfer(cmd, sizeof(cmd), buf, sizeof(buf),1500);
    if(n < 9) return -1;
    if(buf[0]!=0x76 || buf[2]!=0x34 || buf[4]!=0x04) return -2;
    uint32_t len = (buf[5]<<24) | (buf[6]<<16) | (buf[7]<<8) | buf[8];
    return static_cast<int>(len);
}

/**
 * @brief 5 读取完整JPG，分包读取
 * @param cam_addr 设备序号
 * @param total_len ptc_get_jpg_length拿到的总字节
 * @param out_jpg 输出完整jpg二进制
 * @return true成功；会校验FFD8 FFD9头尾
 */
bool ptc_read_full_jpg(uint8_t cam_addr, int total_len, std::vector<uint8_t>& out_jpg)
{
    out_jpg.clear();
    out_jpg.reserve(total_len);
    const int chunk = 2048;
    uint32_t offset = 0;
    while(offset < (uint32_t)total_len)
    {
        uint32_t read_sz = ((offset + chunk) > (uint32_t)total_len) ? (total_len-offset) : chunk;
        //读图片数据指令 56 XX 32 0C 00 0A +4偏移 +4长度 +00 FF
        uint8_t cmd[16] = {
            0x56, cam_addr, 0x32, 0x0C, 0x00, 0x0A,
            (uint8_t)((offset>>24)&0xff),
            (uint8_t)((offset>>16)&0xff),
            (uint8_t)((offset>>8)&0xff),
            (uint8_t)(offset&0xff),

            (uint8_t)((read_sz>>24)&0xff),
            (uint8_t)((read_sz>>16)&0xff),
            (uint8_t)((read_sz>>8)&0xff),
            (uint8_t)(read_sz&0xff),
            0x00, 0xFF
        };
        uint8_t rx_buf[chunk + 40];
        int nr = ptc_raw_transfer(cmd, sizeof(cmd), rx_buf, sizeof(rx_buf), 3000);
        if(nr <= 0)
        {
            std::cerr << "read chunk fail offset=" << offset << "\n";
            return false;
        }
        int data_pos = -1;
        for(int i=0; i < nr-5; i++)
        {
            if(rx_buf[i]==0x76 && rx_buf[i+1]==0x00 && rx_buf[i+2]==0x32 && rx_buf[i+3]==0x00 && rx_buf[i+4]==0x00)
            {
                data_pos = i+5;
                break;
            }
        }
        if(data_pos <0) return false;
        int copy_n = nr - data_pos;
        for(int i=0; i<copy_n; i++)
        {
            out_jpg.push_back(rx_buf[data_pos+i]);
        }
        offset += read_sz;
    }
    //校验jpg文件头尾部
    if(out_jpg.size() <4) return false;
    if( !(out_jpg[0]==0xFF && out_jpg[1]==0xD8) ) return false;
    size_t sz = out_jpg.size();
    if( !(out_jpg[sz-2]==0xFF && out_jpg[sz-1]==0xD9) ) return false;
    return true;
}

/**
 * @brief 6 清空图片缓存指令 56 XX 36 01 03
 * @param cam_addr 设备序号
 * @return true成功
 */
bool ptc_clear_buffer(uint8_t cam_addr)
{
    uint8_t cmd[] = {0x56, cam_addr, 0x36, 0x01, 0x03};
    uint8_t buf[32];
    int n = ptc_raw_transfer(cmd, sizeof(cmd), buf, sizeof(buf),1200);
    if(n>=5 && buf[0]==0x76 && buf[2]==0x36)
    {
        return true;
    }
    return false;
}

/**
 * @brief 7 设置JPG压缩质量
 * @param cam_addr 设备序号
 * @param qval 0x36~0x8F；0x36最高画质，0x8F最低画质
 * @return true成功
 */
bool ptc_set_quality(uint8_t cam_addr, uint8_t qval)
{
    uint8_t cmd[] = {0x56, cam_addr,0x31,0x05,0x01,0x01,0x12,0x04, qval};
    uint8_t buf[32];
    int n = ptc_raw_transfer(cmd, sizeof(cmd), buf, sizeof(buf),1200);
    if(n>0 && buf[0]==0x76 && buf[2]==0x31)
    {
        return true;
    }
    return false;
}

/**
 * @brief 8 设置分辨率大小指令，适用于>=1024*768
 * @param cam_addr 设备序号
 * @param res_code 分辨率编码，如0x1944=1280*720
 * @return true成功
 */
bool ptc_set_resolution(uint8_t cam_addr, uint16_t res_code)
{
    //指令 56 XX 31 05 05 01 00 XX YY
    uint8_t cmd[] = {
        0x56, cam_addr,0x31,0x05,0x05,0x01,0x00,
        (uint8_t)((res_code>>8)&0xff),
        (uint8_t)(res_code &0xff)
    };
    uint8_t buf[32];
    int n = ptc_raw_transfer(cmd, sizeof(cmd), buf, sizeof(buf),1200);
    if(n>0 && buf[0]==0x76 && buf[2]==0x31)
    {
        return true;
    }
    return false;
}

/**
 * @brief 9 修改摄像头串口波特率指令（掉电保存！！RS485最大只能115200）
 * @param cam_addr 设备序号
 * @param baud_code 波特率编码：
 *          0x0DA6 →115200；0xAEC8→9600
 * @note ?调用成功之后，必须调用 serial_set_local_baud 修改本地fd波特率，否则通讯断开
 * @return true指令应答成功
 */
bool ptc_set_baudrate(uint8_t cam_addr, uint16_t baud_code)
{
    //56 XX 31 06 04 02 00 08 XX YY
    uint8_t cmd[] = {
        0x56, cam_addr,0x31,0x06,0x04,0x02,0x00,0x08,
        (uint8_t)((baud_code>>8)&0xff),
        (uint8_t)(baud_code &0xff)
    };
    uint8_t buf[32];
    int n = ptc_raw_transfer(cmd, sizeof(cmd), buf, sizeof(buf),1200);
    if(n>0 && buf[0]==0x76 && buf[2]==0x31)
    {
        return true;
    }
    return false;
}

/**
 * @brief 10 多张连拍指令
 * @param cam_addr 设备序号
 * @param pic_cnt 连拍张数 0~5
 * @param interval_ms 连拍间隔毫秒 0~65535
 * @return true成功
 */
bool ptc_multi_capture(uint8_t cam_addr, uint8_t pic_cnt, uint16_t interval_ms)
{
    //指令 56 XX 88 03 B0 B1 B2
    uint8_t cmd[] = {
        0x56, cam_addr,0x88,0x03,
        pic_cnt,
        (uint8_t)((interval_ms>>8)&0xff),
        (uint8_t)(interval_ms &0xff)
    };
    uint8_t buf[32];
    int n = ptc_raw_transfer(cmd, sizeof(cmd), buf, sizeof(buf),2000);
    if(n>0 && buf[0]==0x76 && buf[2]==0x88)
    {
        return true;
    }
    return false;
}

//==================== FFmpeg辅助：JPG内存解码AVFrame ====================
bool decode_jpeg_to_avframe(const std::vector<uint8_t>& jpg_buf, AVFrame** out_frame)
{
    *out_frame = nullptr;
    AVCodec* dec = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
    if(!dec) return false;
    AVCodecContext* ctx = avcodec_alloc_context3(dec);
    if(avcodec_open2(ctx, dec, nullptr) <0)
    {
        avcodec_free_context(&ctx);
        return false;
    }
    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.data = const_cast<uint8_t*>(jpg_buf.data());
    pkt.size = (int)jpg_buf.size();

    AVFrame* frame = av_frame_alloc();
    int ret = avcodec_send_packet(ctx, &pkt);
    if(ret >=0) ret = avcodec_receive_frame(ctx, frame);
    avcodec_free_context(&ctx);
    if(ret !=0)
    {
        av_frame_free(&frame);
        return false;
    }
    *out_frame = frame;
    return true;
}

//==================== main：只调用封装好的接口函数 ====================
int main()
{
    //1.打开串口
    int fd = ptc_serial_open("/dev/ttyUSB0", 115200);
    if(fd <0) return -1;
    std::cout << "串口打开成功，等待摄像头上电初始化4秒\n";
    sleep(4);

    //---------- 接口调用演示 全部独立函数 ----------
    //查询版本
    std::string ver;
    if(ptc_query_version(CAM_DEFAULT_ADDR, ver))
    {
        std::cout << "摄像头版本: " << ver << "\n";
    }

    //设置最高画质
    ptc_set_quality(CAM_DEFAULT_ADDR, 0x36);
    //设置分辨率：0x1944 =1280*720；0x1988=2304*1296(3MP最大)
    ptc_set_resolution(CAM_DEFAULT_ADDR, 0x1944);

    //多张连拍示例：连拍2张，间隔800ms；图片存1~2号缓存
    //ptc_multi_capture(CAM_DEFAULT_ADDR, 2, 800);

    //单张拍照流程
    std::vector<uint8_t> jpg_data;
    for(int i=0;i<3;i++) //循环抓拍3次演示
    {
        std::cout << "\n==== 第" << i << "次抓拍 ====\n";
        //单张JPG拍照
        if(!ptc_take_photo_jpg(CAM_DEFAULT_ADDR))
        {
            std::cerr << "拍照指令失败\n";
            usleep(800*1000);
            continue;
        }
        //获取图片长度
        int jpg_sz = ptc_get_jpg_length(CAM_DEFAULT_ADDR);
        if(jpg_sz <=0)
        {
            std::cerr << "获取图片长度失败\n";
            usleep(800*1000);
            continue;
        }
        std::cout << "图片字节大小:" << jpg_sz << "\n";
        //读取完整jpg
        if(!ptc_read_full_jpg(CAM_DEFAULT_ADDR, jpg_sz, jpg_data))
        {
            std::cerr << "读取jpg失败\n";
            ptc_clear_buffer(CAM_DEFAULT_ADDR);
            usleep(800*1000);
            continue;
        }
        //保存jpg文件
        std::string fname = std::string("shot_") + std::to_string(i) + ".jpg";
        FILE* fp = fopen(fname.c_str(),"wb");
        if(fp)
        {
            fwrite(jpg_data.data(), 1, jpg_data.size(), fp);
            fclose(fp);
            std::cout << "保存文件:" << fname << "\n";
        }
        //解码到AVFrame，识别入口
        AVFrame* frame = nullptr;
        if(decode_jpeg_to_avframe(jpg_data, &frame))
        {
            std::cout << "[识别模块] 图像分辨率 " << frame->width << "x" << frame->height << "\n";
            //TODO 业务识别算法 frame->data[] YUV数据
            av_frame_free(&frame);
        }
        //清空缓存
        ptc_clear_buffer(CAM_DEFAULT_ADDR);
        sleep(2);
    }

    //复位示例
    //ptc_reset(CAM_DEFAULT_ADDR);
    //sleep(4); //复位之后必须等待3.5s

    //?修改波特率示例（RS485仅支持9600/115200！谨慎使用，改错会失联）
    //if(ptc_set_baudrate(CAM_DEFAULT_ADDR, 0x0DA6))
    //{
    //    serial_set_local_baud(g_serial_fd,115200);
    //}

    ptc_serial_close();
    return 0;
}
