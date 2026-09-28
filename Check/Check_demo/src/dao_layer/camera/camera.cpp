#include "dao_layer/camera/camera.h"
#include <time.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <libgen.h>

extern struct tm *GetRtc_tm(void);

Camera::Camera()
    :
    serial(),
    image(serial)
{

}

int Camera::printf_time(void)
{
	char buf[32] = {0};
	struct timeval tv;
	struct tm      tm;
	
	memset(&tv, 0, sizeof(tv));
	memset(&tm, 0, sizeof(tm));
	gettimeofday(&tv, NULL);
	localtime_r(&tv.tv_sec, &tm);
	strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
	sprintf(buf + strlen(buf), ".%03d", (int)(tv.tv_usec/1000));
	printf("%s\n", buf);
	
	return 0;
}

uint8_t Camera::GetVersionFun(void)
{   uint8_t recv_buf[50] = "";
    int ret = 0;

    int tx = serial.PortSend(
        cByteGetVersion,
        sizeof(cByteGetVersion)
    );
    
    if(tx < 0)
    {
        printf("PortSend failed, tx=%d\n", tx);
        return 0;
    }
    else
    {
        printf("PortSend success, tx=%d\n", tx);
    }
    
    ret = serial.recv_compare(cByteRetVersion,recv_buf);
    
    if(ret <= 0)
    {
        printf("recv_compare failed, ret=%d\n", ret);
        return 0;
    }
    else
    {
        printf("recv_compare success, len=%d\n", ret);
        printf("CAMVER:%s\n",&recv_buf[5]);  //第5字节开始是版本名称
    }

    return (ret > 0) ? 1 : 0;
}

uint8_t Camera::SetResolutionFun(uint8_t *pResCmd)
{
    uint8_t recv_buf[50] = "";
    int ret = 0;
    serial.PortSend(pResCmd,9);
    ret = serial.recv_compare(cBytePTC2MRetImageSize,recv_buf);
    printf("%s %s\n",__func__,((ret > 0) ? "success" : "fail"));
    return (ret > 0) ? 1 : 0;
}

int Camera::SetOSDText(OsdConf_t * pOsd)
{
    int ret  = 0;
    uint8_t _CmdLen = 0;
    uint8_t recv_buf[50] = "";
  
    /*16位需交换一下字节序*/
	  pOsd->u16Color = SWAP16(pOsd->u16Color);      /*字体颜色*/
	  pOsd->u16BkColor = SWAP16(pOsd->u16BkColor);  /*背景颜色*/
	  pOsd->u16x = SWAP16(pOsd->u16x);
	  pOsd->u16y = SWAP16(pOsd->u16y);
	
	  _CmdLen = pOsd->u8Length + 12;
	  cByteOSDCtrl[3] = (uint8_t)((_CmdLen > 255) ? 255 : _CmdLen);//整条命令长度
	  memcpy((uint8_t *)&cByteOSDCtrl[4],(uint8_t *)pOsd,sizeof(OsdConf_t));
    
    serial.PortSend(cByteOSDCtrl, pOsd->u8Length + 16);
    ret = serial.recv_compare(cByteRetOSDCtrl,recv_buf);

    return (uint32_t)ret;
}

uint8_t Camera::TakephotoFun(void)
{
    uint8_t recv_buf[50] = "";
    int ret = 0;

    int tx = serial.PortSend(cBytePhoto,sizeof(cBytePhoto));

    if(tx < 0)
    {
        printf("Takephoto send failed\n");
        return 0;
    }

    ret = serial.recv_compare(cByteRetPhoto,recv_buf);
    
    printf("%s %s\n",__func__,((ret > 0) ? "success" : "fail"));
    return (ret > 0) ? 1 : 0;
}

void Camera::CameraDemoApp(void)
{
    mkdir("/home/ztl/program/Check/Check_demo/picture",0777);

    int Portfd = serial.PortOpen(&sPortInfo);

    if(Portfd < 0)
    {
        printf("Camera serial open failed\n");
        return;
    }

    printf("Camera serial open success fd=%d\n",Portfd);
    
    usleep(500000); 
    
    uint32_t length = 0;
    uint32_t AddrOff = 0, NByte = 20480, NTimes = 0, LastByte = 0;
    uint8_t  bCompleteRead = 1;
    uint8_t  bUseOsd = 1;
    uint8_t *pMemJPG = NULL;
    uint32_t rxcount = 0, total_size = 0;
    uint32_t i = 0;
    int fd = 0;
    int wret = 0;
    char time_buf[30] = "";
    char filename[64];
    char pFilepath[256];


    //=====生成时间戳文件名=====
    image.getPhotoFileName(filename,sizeof(filename));

    //去掉前面的 "./"
    snprintf(pFilepath,sizeof(pFilepath),"/home/ztl/program/Check/Check_demo/picture/%s",filename + 2);
    printf_time();
    printf("save image to file: %s\n", pFilepath);

    if(!GetVersionFun()) /*发一条命令确认通讯正常*/
    {
        return ;
    }
    SetResolutionFun(cByteImageSize2560x1440); // 设置拍摄画质
    
    if(bUseOsd) /*使用OSD叠加字符时，需在拍照前设置文本*/
    {
        uint8_t idx = 0;
        strftime(time_buf, sizeof(time_buf), "%Y%m%d %H%M%S", GetRtc_tm());
        _sOsdConf[3].u8Length = strlen(time_buf);
				strcpy((char *)_sOsdConf[3].u8Data,time_buf);
        for(idx = 0; idx < 4; idx++)
        {
            SetOSDText(&_sOsdConf[idx]);
        }
    }
    if(!TakephotoFun()) 
    {
        return ;
    }

    //====关键延时！拍照后等待相机编码JPG（新增）====
    usleep(600000);   //600ms，不够就加到 800000
    
    //usleep(3000000); //3秒，485 给1080P足够编码时间

    length = image.GetPhotoLengthFun();
    if(!length)
    {
        return ;   
    }
    if(bCompleteRead) /*一次性读取*/
    {
        NByte    = length;
        LastByte = length;
        AddrOff  = 0;
        NTimes   = 1;
    }
    else /*分次读取*/
    {
        NTimes = length / NByte;
        LastByte = length % NByte;
        NTimes += (LastByte > 0) ? 1 : 0;
    }

    pMemJPG = (uint8_t *)malloc(length + 10);
    fd = open(pFilepath,O_RDWR|O_CREAT,0777);
    if(pMemJPG && fd > 0)
    {
        for(i = 0; i < NTimes; )
        {
            if((i + 1) == NTimes) {NByte = LastByte;}
            rxcount = image.ReadPhotoData(AddrOff,NByte,pMemJPG);
            printf("rxcount:%d\n",rxcount);
      			if(!rxcount)
      			{
                      //接收失败，释放资源，直接return！！不再执行后面代码
                      close(fd);
                      free(pMemJPG);
                      pMemJPG = NULL;
                      close(fd);
                      remove(pFilepath); //删除残缺文件
                      printf("picture rx outtime !\n");
                      return;    //<<<<重点！跳出整个函数，而不是break
      			}
            else if(rxcount > 0 && rxcount < NByte)
            {
                printf("miss data : length: %d\n",rxcount);
                continue;
            }
            else
            {
                wret = write(fd,pMemJPG + 5,NByte);
                total_size += wret;
                AddrOff += NByte;
                i++;
            }
         }
        
      close(fd);
      free(pMemJPG);
      pMemJPG = NULL;
    }
    printf("NTimes:%d\n",NTimes);
    printf("JPEG length: %d\n",length);
    printf("file total_size:%d,camera demo finish\n",total_size);
}
