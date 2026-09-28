#include "dao_layer/camera/camera_image.h"
#include "dao_layer/camera/camera_serial.h"
#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

CameraImage::CameraImage(CameraSerial &serial)
    :
    serial(serial)
{

}

int CameraImage::getPhotoFileName(char *filePath, int bufSize)
{
	  struct timeval tv;
	  struct tm tm;

	  gettimeofday(&tv, NULL);
	  localtime_r(&tv.tv_sec, &tm);

	  snprintf(filePath, bufSize,
		"./%04d%02d%02d_%02d%02d%02d_%03d.jpg",
		tm.tm_year + 1900,
		tm.tm_mon + 1,
		tm.tm_mday,
		tm.tm_hour,
		tm.tm_min,
		tm.tm_sec,
		(int)(tv.tv_usec / 1000));

	return 0;
}

uint32_t CameraImage::GetPhotoLengthFun(void)
{
    uint8_t recv_buf[50] = "";
    int  ret = 0;
    uint32_t length = 0;
    
    serial.PortSend(cByteReadLength,sizeof(cByteReadLength));
    ret = serial.recv_compare(cByteRetReadLength,recv_buf);
    length = BIG_ENDIAN_UINT32(&recv_buf[5]);

    printf("%s %s\n",__func__,((ret > 0) ? "success" : "fail"));
    if(ret > 0) printf("length:%u\n",(unsigned int)length);

    return length;
}

uint32_t CameraImage::ReadPhotoData(uint32_t Addr,uint32_t Nbytes,uint8_t * pJPGData)
{
    struct sCmd_t{
        uint32_t AddrOff;
        uint32_t OnceByte;
    }__attribute__((packed));

    struct sCmd_t sCmd;
    int ret  = 0;
    
    sCmd.AddrOff = SWAP32(Addr);
    sCmd.OnceByte = SWAP32(Nbytes);
    memcpy((uint8_t *)&cByteReadBuf[6],(uint8_t *)&sCmd,sizeof(sCmd));
    
    serial.PortSend(cByteReadBuf,sizeof(cByteReadBuf));
    ret = serial.recv_compare(cByteRetReadBuf,pJPGData);

    return (uint32_t)ret;
}

struct tm * GetRtc_tm(void)
{
    struct timeval tv;
    static struct tm tm;
	
    memset(&tv, 0, sizeof(tv));
	  memset(&tm, 0, sizeof(tm));
	  gettimeofday(&tv, NULL);
	  localtime_r(&tv.tv_sec, &tm);
    
    return &tm;
}
