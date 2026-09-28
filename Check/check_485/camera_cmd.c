#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>  //stat
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>  //O_RDWR
#include <time.h>
#include <sys/time.h>



#define SWAP16(x) (((x >> 8) & 0xff) | ((x & 0xff ) << 8))   /*高低字节交换*/

#define SWAP32(x)		((((x) & 0x000000ff) << 24) | \
				 (((x) & 0x0000ff00) <<  8) | \
				 (((x) & 0x00ff0000) >>  8) | \
				 (((x) & 0xff000000) >> 24)) 

#define BIG_ENDIAN_UINT32(x) ((*(x) << 24) | (*(x + 1) << 16) | (*(x + 2) << 8) | *(x + 3))
#define BIG_ENDIAN_UINT16(x) ((*(x) << 16) | *(x + 1))

int printf_time(void)
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

int getPhotoFileName(char *filePath, int bufSize)
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


/*查询版本号*/
uint8_t cByteGetVersion[] = {
	0x56,
	0x00,
	0x11,
	0x00
};

/*返回 76 00 11 00 0B PTC1M3 1.00*/
/*PTC1M3 1.01 ->版本变更*/
uint8_t cByteRetVersion[50] = {
	0x76,
	0x00,
	0x11,
	0x00,
	0x0B,/*ret version length*/
	// 0x50,
	// 0x54,
	// 0x43,
	// 0x31,
	// 0x4D,
	// 0x33,
	// 0x20,
	// 0x31,
	// 0x2E,
	// 0x30,
	// 0x31
};
//拍照 --- 56 00 36 01 00
//返回 --- 76 00 36 00 00
uint8_t cBytePhoto[] = 
{
	0x56,
	0x00,
	0x36,
	0x01,
	0x00 //JPG格式
}; 

uint8_t cBytePhotoYUV[] = 
{
	0x56,
	0x00,
	0x36,
	0x01,
	0x0A, //yuv格式
}; 
uint8_t cByteRetPhoto[5] = 
{
	0x76,
	0x00,
	0x36,
	0x00,
	0x00
}; 
uint8_t cByteRetPhotoError[5]=
{
    0x76,
    0x00,
    0x36,
    0x00,
    0x0E
};

//?á3¤?è --- 56 00 34 01 00 
//返回   --- 76 00 34 00 04 xx xx xx xx
uint8_t cByteReadLength[] = 
{
	0x56,
	0x00,
	0x34,
	0x01,
	0x00
}; 

uint8_t cByteRetReadLength[9] = 
{
	0x76,
	0x00,
	0x34,
	0x00,
	0x04,
	0x00, //??×??ú
	0x00, //
	0x00, //
	0x00  //μí×??ú
};

/*读图片长度扩展指令*/
uint8_t cByteExtReadLength[] = 
{
	0x56,
	0x00,
	0x34,
	0x02,
	0x00,
	0x00,//0-5号图片缓存
}; 

/*应答读图片长度扩展指令*/
uint8_t cByteRetExtReadLength[] = 
{
	0x76,
	0x00,
	0x34,
	0x00,
	0x0A,
	0x00, //4字节图片长度(大端表示)
	0x00, 
	0x00, 
	0x00,
	0x00, //2字节图片宽度,大端表示
	0x00,
	0x00, //2字节图片高度,大端表示
	0x00, 
	0x00, //2字节图片校验(CRC16/Modbus),大端表示
	0x00,
};


//读Buf  --- 56 00 32 0C 00 0A 00 00 MM MM 00 00 KK KK XX XX 
//返回   --- 76 00 34 00 04 xx xx xx xx
uint8_t cByteReadBuf[] = 
{
	0x56,
	0x00,
	0x32,
	0x0C,
	0x00,
	0x0A,
    0x00,0x00,0x00,0x00,  //custom postion
    0x00,0x00,0x00,0x00,  //read length
    0x00,
    0xFF,
}; 
uint8_t cByteRetReadBuf[]=
{   0x76,
    0x00,
    0x32,
    0x00,
    0x00
};
//清除缓存
uint8_t cByteCleanPhoto[] = 
{
	0x56,
	0x00,
	0x36,
	0x01,
	0x03
}; 

uint8_t cByteRetCleanPhoto[] = 
{
	0x76,
	0x00,
	0x36,
	0x00,
	0x00
}; 
//í￡?1???? --- 56 00 37 01 03
//返回	   --- 76 00 37 00 00
uint8_t cByteStopPhoto[] = 
{
	0x56,
	0x00,
	0x37,
	0x01,
	0x03
}; 

uint8_t cByteRetStopPhoto[] = 
{
	0x76,
	0x00,
	0x37,
	0x00,
	0x00
}; 
//复位 --- 56 00 26 00
//返回 --- 76 00 26 00
uint8_t cByteReset[] = 
{
	0x56,
	0x00,
	0x26,
	0x00
}; 

uint8_t cByteRetReset[4] = 
{
	0x76,
	0x00,
	0x26,
	0x00
}; 
//í???′óD?(160*120) --- 56 00 31 05 04 01 00 19 22
//返回			    --- 76 00 31 01 00
uint8_t cByteImageSize160x120[] = 
{
	0x56,
	0x00,
	0x31,
	0x05,
	0x04,
	0x01,
	0x00,
	0x19,
	0x22,
}; 

//í???′óD?(320*240) --- 56 00 31 05 04 01 00 19 11
//返回			    --- 76 00 31 01 00
uint8_t cByteImageSize320x240[] = 
{
	0x56,
	0x00,
	0x31,
	0x05,
	0x04,
	0x01,
	0x00,
	0x19,
	0x11
}; 

//í???′óD?(640*480) --- 56 00 31 05 04 01 00 19 00
//返回			    --- 76 00 31 01 00
uint8_t cByteImageSize640x480[] = 
{
	0x56,
	0x00,
	0x31,
	0x05,
	0x04,
	0x01,
	0x00,
	0x19,
	0x00
}; 

//í???′óD?(1024*768) --- 56 00 31 05 05 01 00 19 33
uint8_t cByteImageSize1024x768[] =
{
    0x56,
    0x00,
    0x31,
    0x05,
    0x05,
    0x01,
    0x00,
    0x19,
    0x33
};
uint8_t cByteImageSize1280x720[] =
{
    0x56,
    0x00,
    0x31,
    0x05,
    0x05,
    0x01,
    0x00,
    0x19,
    0x44
};
uint8_t cByteImageSize1280x960[] =
{
    0x56,
    0x00,
    0x31,
    0x05,
    0x05,
    0x01,
    0x00,
    0x19,
    0x55
};

uint8_t cByteImageSize1920x1080[] =
{
    0x56,
    0x00,
    0x31,
    0x05,
    0x05,
    0x01,
    0x00,
    0x19,
    0x66
};

uint8_t cByteImageSize1600x1200[] =
{
    0x56,
    0x00,
    0x31,
    0x05,
    0x05,
    0x01,
    0x00,
    0x19,
    0x77
};

uint8_t cByteImageSize2304x1296[] =
{
    0x56,
    0x00,
    0x31,
    0x05,
    0x05,
    0x01,
    0x00,
    0x19,
    0x88
};


uint16_t ImageSize[][2] = {
    2304,1296,
    1600,1200,
    1920,1080,
    1280,960,
    1280,720,
    1024,768,
    640,480,
    320,240,
    160,120
};

uint8_t cBytePTC2MRetImageSize[5] = 
{
	0x76,
	0x00,
	0x31,
	0x01,
	0x00
};

uint8_t cByteDefBaud9600[] = 
{
    0x56,
    0x00,
    0x31,
    0x06,
    0x04,
    0x02,
    0x00,
    0x08,
    0xAE,
    0xC8,
};

uint8_t cByteDefBaud19200[] = 
{
    0x56,
    0x00,
    0x31,
    0x06,
    0x04,
    0x02,
    0x00,
    0x08,
    0x56,
    0xE4,
};

uint8_t cByteDefBaud38400[] = 
{
    0x56,
    0x00,
    0x31,
    0x06,
    0x04,
    0x02,
    0x00,
    0x08,
    0x2A,
    0xF2,
};

uint8_t cByteDefBaud57600[] = 
{
    0x56,
    0x00,
    0x31,
    0x06,
    0x04,
    0x02,
    0x00,
    0x08,
    0x1C,
    0x4C,
};

uint8_t cByteDefBaud115200[] = 
{
    0x56,
    0x00,
    0x31,
    0x06,
    0x04,
    0x02,
    0x00,
    0x08,
    0x0D,
    0xA6,
};

/*增加230400*/
uint8_t cByteDefBaud230400[] = 
{
	0x56,
	0x00,
	0x31,
	0x06,
	0x04,
	0x02,
	0x00,
	0x08,
	0xEE,
	0xA1,
};

/*增加460800*/
uint8_t cByteDefBaud460800[] = 
{
	0x56,
	0x00,
	0x31,
	0x06,
	0x04,
	0x02,
	0x00,
	0x08,
	0xEE,
	0xA2,
};

/*增加921600*/
uint8_t cByteDefBaud921600[] = 
{
	0x56,
	0x00,
	0x31,
	0x06,
	0x04,
	0x02,
	0x00,
	0x08,
	0xEE,
	0xA3,
};

/*为了能区分30万像素 PTC08的设置指令，在PTC1M3 1.05
PTC2M0 1.02以上版本将使用下面的指令设置230400以上的波特率，原设置指令保留
*/
/*230400设置指令2*/
uint8_t cByteDefBaudExt230400[] = 
{
	0x56,
	0x00,
	0x31,
	0x06,
	0x05, /*在VC0706协议里，0x05表示存储到SPI器件*/
	0x02,
	0x00,
	0x08,
	0xEE,
	0xA1,
};

/*460800设置指令2*/
uint8_t cByteDefBaudExt460800[] = 
{
	0x56,
	0x00,
	0x31,
	0x06,
	0x05,
	0x02,
	0x00,
	0x08,
	0xEE,
	0xA2,
};

/*921600设置指令2*/
uint8_t cByteDefBaudExt921600[] = 
{
	0x56,
	0x00,
	0x31,
	0x06,
	0x05,
	0x02,
	0x00,
	0x08,
	0xEE,
	0xA3,
};

uint8_t cByteRetDefBaud[] = 
{
    0x76,
    0x00,
    0x31,
    0x00,
    0x00,
}; 
//DT??é???í·DòáDo?  --- 56 00 31 05 04 01 00 06
//返回              --- 76 00 31 00 00
uint8_t cByteChangeNum[] = 
{
	0x56,
	0x00,
	0x31,
	0x05,
	0x04,
	0x01,
	0x00,
	0x06
};
uint8_t cByteRetChangeNum[] = 
{
    0x76,
    0x00,
    0x31,
    0x00,
    0x00
};
//56 00 3E 03 00 01 01 
//76 00 3E 00 00
uint8_t cByteStandby[]=
{
    0x56,
    0x00,
    0x3E,
    0x03,
    0x00,
    0x01,
    0x01
};
uint8_t cByteNormal[]=
{
    0x56,
    0x00,
    0x3E,
    0x03,
    0x00,
    0x01,
    0x00
};
uint8_t cByteRetStandby[5]=
{
    0x76,
    0x00,
    0x3E,
    0x00,
    0x00
};
uint8_t cByteRetNormal[5]=
{
    0x76,
    0x00,
    0x3E,
    0x00,
    0x00
};
uint8_t cByteRetpowererror[5]=
{
    0x76,
    0x00,
    0x3F,
    0x03,
    0x00
};
uint8_t cByteCheckNum[]=
{
    0x56,
    0x00,
    0x11,
    0x00
};
uint8_t cByteRetCheckNum[4]=
{
    0x76,
    0x00,
    0x11,
    0x00
};
uint8_t cByteStartMD[]=
{
	0x56,
	0x00,
	0x37,
	0x01,
	0x01
};
uint8_t cByteStopMD[]=
{
	0x56,
	0x00,
	0x37,
	0x01,
	0x00
};
uint8_t cByteRetMD[]=
{
	0x76,
	0x00,
	0x37,
	0x00,
	0x00
};
uint8_t cByteRetMDError[]=
{
	0x76,
	0x00,
	0x37,
	0x03,
	0x00
};
uint8_t cByteQuit[]=
{
	0x56,
	0x00,
	0x71,
	0x75,
	0x69,
	0x74
};

uint8_t cByteMDSensitivity[]={0x56,0x00,0x31,0x05,0x01,0x01,0x1A,0x6E};
uint8_t cByteRetMDSensitivity[]={0x76,0x00,0x31,0x00,0x00};

uint8_t cByteRetQuit[4]=
{
	0x76,
	0x00,
	0x71,
	0x00
};

/*增加对灯光的控制命令0x85*/
uint8_t cByteLEDEnable[]  = {0x56,0x00,0x85,0x01,0x01};
uint8_t cByteLEDDisable[] = {0x56,0x00,0x85,0x01,0x00};
uint8_t cByteRetLED[] = {0x76,0x00,0x85,0x00};
/*设置OSD字符*/
uint8_t cByteOSDCtrl[512] = 
{
	0x56,//帧头
	0x00,//序号
	0x86,//命令
	0x16,//命令长度
	0x01,//显示开关
	0x00,//第N行,(0-3)
	0x01,//字体
	0x00,0x00,//x坐标
	0x00,0x00,//y坐标
	0x00,0x7C,//字体颜色，蓝色 RGB555 顺序
	0xff,0xff,//背景颜色，白色 
	0x0A,//文字长度
	0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,//文字内容
};

uint8_t cByteRetOSDCtrl[] = {
	0x76,
	0x00,
	0x86,
	0x01,
	0x00,  //第N行
};

uint8_t cByteMonitorAttr[] = {
	0x56,
	0x00,
	0x87, //移动侦测连拍指令，配置移动侦测打开指令使用
	0x04, //长度2个字节
	0x03, //触发移动侦测后的拍照数量
    0x55, //移动侦测图片时间的保留时间，单位秒
	0x00, //2个字节，移动侦测间隔时间，单位毫秒
	0x10, 
};

uint8_t cByteRetMonitorAttr[] = {
	0x76,
	0x00,
	0x87,
	0x01,
	0x00,
};

uint8_t cBytePhotoContiue[] = {
	0x56,
	0x00,
	0x88, //自由连拍指令
	0x03, //长度3个字节
	0x03, //拍照数量
	0x00, //2个字节，移动侦测间隔时间，单位毫秒
	0x10, 
};

uint8_t cByteRetPhotoContiue[] = {
	0x76,
	0x00,
	0x88,
	0x01,
	0x00,
};


/*设置压缩率*/
uint8_t cByteSetCompress[] = {
  0x56,
	0x00,
	0x31,
	0x05,
	0x01,
	0x01,
	0x12, //固定地址0x1204
	0x04,
	0x36  //压缩率范围(PTC1M3,PTC2M0 :0x36 - 0x90)
};

uint8_t cByteRetSetCompress[] = {
	0x76,
	0x00,
	0x31,
	0x00,
	0x00
};

/*设置图像裁剪区域*/
uint8_t cByteSetCrop[] = {
  0x56,
	0x00,
	0x8B,
	0x0A, //数据长度
	0x01, //子命令，0x01设置裁剪
	0x01, //裁剪开关 0x01打开，0x00关闭
	0x00, //起始坐标x
	0x00,
	0x00, //起始坐标y
	0x00,
	0x64, //裁剪宽度2字节
	0x00,
	0x64, //裁剪高度2字节
	0x64,
};

uint8_t cByteRetSetCrop[] = {
	0x76,
	0x00,
	0x8B,
	0x02,
	0x01,//子命令 设置裁剪
	0x00,//状态码 0x00设置成功
};


/*读取裁剪*/
uint8_t cByteReadCrop[] = {
  0x56,
	0x00,
	0x8B,
	0x01,
	0x02
};

uint8_t cByteRetReadCrop[] = {
	0x76,
	0x00,
	0x8B,
	0x0E,//长度，不同子命令，长度不一样
	0x02,//0x01:设置裁剪  0x02:查询裁剪
	0x00, //打开/关闭裁剪
	0x00, //x
	0x00,
  0x00, //y
	0x00,
	0x02, //crop width  640
	0x80,
	0x01, //crop height 480
	0xE0,
	0x02, //图片分辨率640
	0x80,
	0x01, //图片分辨率480
	0xE0,
};

/*彩转灰指令*/
uint8_t cByteSetColor[] = {
	0x56,
	0x00,
	0x8C,
	0x01,
	0x01, //00 彩色   01 灰色
};

uint8_t cByteRetSetColor[] = {
	0x76,
	0x00,
	0x8C,
	0x01,
	0x00,
};

/*查询指令*/
uint8_t cByteGetParam[] = {
	0x56,
	0x00,
	0x8D,
	0x01,
	0x00, /*查询光敏电阻状态*/
};

uint8_t cByteRetParam[] = {
	0x76,
	0x00,
	0x8D,
	0x02, /*返回指令长度*/
	0x00, /*子命令*/
	0x00, /*返回状态*/
};



/*上电抓拍设置指令*/
uint8_t cByteSetParamSnap[] = {
  0x56,
	0x00,
	0x91, //写配置指令
	0x04, //数据长度
	0x01, //子命令：设置上电连拍
	0x05, //连拍数量
	0x01, //连拍时间，单位ms, 大端表示 0x01F4 = 500ms
	0xF4, 
};

/*应答上电抓拍指令*/
uint8_t cByteRetSetParamSnap[] = {
  0x76,
	0x00,
	0x91, //写配置指令
	0x01, //数据长度
	0x00, //0x00 / 0x01 设置成功/失败
};

/*读取上电抓拍指令*/
uint8_t cByteReadParamSnap[] = {
	0x56,
	0x00,
	0x90, //读配置指令
	0x01, //数据长度
	0x01, //子命令：查询上电连拍
};

/*应答读取上电抓拍配置指令*/
uint8_t cByteRetReadParamSnap[] = {
	0x76,
	0x00,
	0x90, //读配置指令
	0x04, //数据长度
	0x01, //子命令：上电连拍
	0x05, //拍照数量
	0x01, //连拍间隔时间
	0xF4
};

uint8_t cByteUploadParamSnap[] = {
	0x76,
	0x00,
	0x92, //主动上报指令
	0x02, //固定长度
	0x01, //子命令：上电连拍  
	0x05, //0-5已拍照数量,0xee 错误
};



/*2022-06-12 增加移动/人形/车辆检测指令*/

/*侦测配置指令*/
uint8_t cByteSetParamDetect[] = {
  0x56,
	0x00,
	0x91, //写配置指令
	0x0C, //数据长度
	0x02, //子命令：设置智能侦测指令
	0x00, //开机侦测  : 00-禁用(默认)  01-启用
	0x00, //侦测类型  : 00-移动侦测(默认)  01-人形侦测 02-车辆侦测
	0x03, //侦测灵敏度: 范围(0-4) 4最灵敏，默认值3 
	0x00, //侦测模式  : 00-只侦测(默认)  01-侦测+自动抓拍
	0x01, //侦测报警值: 只在人型/车辆侦测有效，侦测到多少人(范围1-19)/多少车(范围1-5)才上报
	0x02, //侦测距离  : 只在人形/车辆侦测有效，02-10米(默认)
	0x01, //目标框选  : 00-关闭 01-打开(默认)
	0x03, //触发侦测后的拍照数量: 在侦测模式=0x01时有效，范围（1-5）
	0x00, //抓拍间隔时间:2个字节大端表示,在侦测模式=0x01时有效，单位毫秒
	0x64, 
	0x0A, //下次侦测时间:即抓拍图片的缓存时间，在侦测模式=0x01时有效，单位秒 范围(1-255)

};

/*应答侦测设置指令*/
uint8_t cByteRetSetParamDetect[] = {
	0x76,
	0x00,
	0x91, //写配置指令
	0x02, //数据长度
	0x02, //子命令:侦测指令类
	0x00, //0x00 / 0x01 设置成功/失败
};

/*读取侦测参数指令*/
uint8_t cByteReadParamDetect[] = {
	0x56,
	0x00,
	0x90, //读配置指令
	0x01, //数据长度
	0x02, //子命令：侦测参数指令
};

/*应答读取侦测参数指令*/
uint8_t cByteRetReadParamDetect[] = {
	0x76,
	0x00,
	0x90, //读配置指令
	0x0C, //数据长度
	0x02, //子命令：侦测参数指令
	0x00, //开机侦测  : 00-禁用(默认)  01-启用
	0x00, //侦测类型  : 00-移动侦测(默认)  01-人形侦测 02-车辆侦测
	0x03, //侦测灵敏度: 范围(0-4) 4最灵敏，默认值3 
	0x00, //侦测模式  : 00-只侦测(默认)  01-侦测+自动抓拍
	0x01, //侦测报警值: 只在人型/车辆侦测有效，侦测到多少人(范围1-19)/多少车(范围1-5)才上报
	0x02, //侦测距离  : 只在人形/车辆侦测有效，02-10米(默认)
	0x01, //目标框选  : 00-关闭 01-打开(默认)
	0x03, //触发侦测后的拍照数量: 在侦测模式=0x01时有效，范围（1-5）
	0x00, //抓拍间隔时间:2个字节大端表示,在侦测模式=0x01时有效，单位毫秒
	0x64, 
	0x0A, //下次侦测时间:即抓拍图片的缓存时间，在侦测模式=0x01时有效，单位秒 范围(1-255)
};

/*上报侦测结果*/
uint8_t cByteUploadParamDetect[] = {
	0x76,
	0x00,
	0x92, //主动上报指令
	0x04, //固定长度
	0x02, //子命令：侦测指令
	0x01, //侦测类型 00-移动侦测 01-人形侦测 02-车辆侦测
	0x00, //侦测模式 00-只侦测  01-侦测+抓拍
	0x05, //侦测模式 = 0x00时 表示侦测结果 人数/车数 侦测模式 = 0x01 时表示侦测的图片数量
};

/*移动侦测上报*/
uint8_t cByteUploadMoveDetect[] = {
	0x76,
	0x00,
	0x39,
	0x00, //固定0字节
	0x05, //当侦测模式=0x00时，固定字节0,当侦测模式=0x01时，表示抓拍的图片数量
};

/*人形/车辆侦测上报*/
uint8_t cByteUploadExtDetect[] = {
	0x76,
	0x00,
	0x39,
	0x03, //固定长度
	0x01, //侦测类型: 01-人形侦测 02-车辆侦测
	0x00, //侦测模式: 00-只侦测  01-侦测+抓拍
	0x05, //侦测目标: 当侦测模式 = 0x00时 表示侦测结果 人数/车数 当侦测模式 = 0x01 时表示侦测后抓拍的图片数量
};


/*读取默认参数指令*/
uint8_t cByteReadParamDefault[] = {
	0x56,
	0x00,
	0x90, //读配置指令
	0x01, //数据长度
	0x00, //子命令：查询默认参数
};

/*应答读取默认参数指令*/
uint8_t cByteRetReadParamDefault[] = {
	0x76,
	0x00,
	0x90, //读配置指令
	0x07, //数据长度
	0x00, //子命令：设置默认参数
	0x00, //1byte 表示序号
	0x66, //1byte 表示分辨率 0x66:1920x1080
	0x0D, //2byte 大端表示 0xAEC8 波特率 9600
	0xA6,
	0x36, //1byte 压缩率，范围(0x36-0x90)
	0x01, //1byte 侦测类型 0x00 移动侦测 0x01 人人形侦测 0x02车辆侦测
};


/*设置ISP图像指令*/
uint8_t cByteSetParamISP[] = {
	0x56,
	0x00,
	0x91, //写配置指令
	0x0C, //数据长度
	0x04, //子命令：ISP图像设置
	0x00, //1byte 日夜模式: 00:白天 01:夜晚
	0x80, //1byte 亮度 范围(0-255,以下参数的范围均为0-255)
	0x80, //1byte 对比度
	0x80, //1byte 清晰度
	0x80, //1byte 饱和度
	0x80, //1byte 色调
	0x00, //1byte 白平衡 00:自动模式 01:手动模式
	0x00, //2byte 白平衡手动模式参数
	0x00,
	0x80, //1byte 曝光补偿
	0x00, //1byte 强光抑制, 范围[0-10],0表示关闭 
};


/*应答设置ISP图像指令*/
uint8_t cByteRetSetParamISP[] = {
	0x76,
	0x00,
	0x91, //读配置指令
	0x02, //数据长度
	0x04, //子命令：ISP图像设置
	0x00, //00: 设置成功 01:设置失败
};


/*读取ISP图像设置指令*/
uint8_t cByteReadParamISP[] = {
	0x56,
	0x00,
	0x90, //读配置指令
	0x02, //数据长度
	0x04, //子命令：ISP图像设置
	0x00, //1byte 日夜模式: 00:白天 01:夜晚
};


/*应答ISP图像设置指令*/
uint8_t cByteRetReadParamISP[] = {
	0x76,
	0x00,
	0x90, //读配置指令
	0x0C, //数据长度
	0x04, //子命令：ISP图像设置
	0x00, //1byte 日夜模式: 00:白天 01:夜晚
	0x80, //1byte 亮度 范围(0-255,以下参数的范围均为0-255)
	0x80, //1byte 对比度
	0x80, //1byte 清晰度
	0x80, //1byte 饱和度
	0x80, //1byte 色调
	0x00, //1byte 白平衡 00:自动模式 01:手动模式
	0x00, //2byte 白平衡手动模式参数
	0x00,
	0x80, //1byte 曝光补偿
	0x00, //1byte 强光抑制, 范围[0-10],0表示关闭 
};

/*复位ISP默认设置指令*/
uint8_t cByteResetParamISP[] = {
	0x56,
	0x00,
	0x91, //写配置指令
	0x02, //数据长度
	0x04, //子命令：ISP图像设置
	0x00, //1byte 日夜模式: 00:白天 01:夜晚
};

/*应答复位ISP图像默认参数指令*/
uint8_t cByteRetResetParamISP[] = {
	0x76,
	0x00,
	0x91, //设置配置指令
	0x0C, //数据长度
	0x04, //子命令：ISP图像设置
	0x00, //1byte 日夜模式: 00:白天 01:夜晚
	0x80, //1byte 亮度 范围(0-255,以下参数的范围均为0-255)
	0x80, //1byte 对比度
	0x80, //1byte 清晰度
	0x80, //1byte 饱和度
	0x80, //1byte 色调
	0x00, //1byte 白平衡 00:自动模式 01:手动模式
	0x00, //2byte 白平衡手动模式参数
	0x00,
	0x80, //1byte 曝光补偿
	0x00, //1byte 强光抑制, 范围[0-10],0表示关闭 
};


/*读取序号*/
uint8_t cByteReadParamSerial[] = {
	0x56,
	0xff, //固定序号
	0x90, //读配置指令
	0x01, //数据长度
	0x21, //子命令：查询序号
};

/*应答读取序号指令*/
uint8_t cByteRetReadParamSerial[] = {
	0x76,
	0xff, //固定序号
	0x90, //读配置指令
	0x02, //数据长度
	0x21, //子命令：设置默认参数
	0x00, //1byte 表示序号
};

/*设置图像格式指令*/
uint8_t cByteISPImageFormatCmd[] = {
	0x56,
	0x00,
	0x93, //图像设置指令
	0x03, //数据长度
	0x03, //图像格式设置指令
	0x01, //写参数
	0x00, //格式: 0x00:jpg 0x01:yuv(nv12) 0x02:bmp
};

/*应答设置图像格式指令*/
uint8_t cByteRetISPImageFormatCmd[] = {
	0x76,
	0x00,
	0x93, //图像相关指令
	0x03, //数据长度
	0x03, //图像格式设置指令
	0x01, //写参数
	0x00, //设置成功
};

/*设置图像曝光时间指令*/
uint8_t cByteSetExporuserTimeCmd[] = {
	0x56,
	0x00,
	0x93, //图像设置指令
	0x04, //数据长度
	0x01, //图像曝光行设置指令
	0x01, //写参数
	0x00, //2byte，范围0-1355 
	0x00,
};

/*应答设置图像曝光时间指令*/
uint8_t cByteRetSetExporuserTimeCmd[] = {
	0x76,
	0x00,
	0x93, //图像相关指令
	0x03, //数据长度
	0x01, //图像格式设置指令
	0x01, //写参数
	0x00, //设置成功
};

/*获取图像曝光时间指令*/
uint8_t cByteGetExporuserTimeCmd[] = {
	0x56,
	0x00,
	0x93, //图像设置指令
	0x02, //数据长度
	0x01, //图像曝光行设置指令
	0x00, //读参数
};

/*应答获取图像曝光时间指令*/
uint8_t cByteRetGetExporuserTimeCmd[] = {
	0x76,
	0x00,
	0x93, //图像设置指令
	0x04, //数据长度
	0x01, //图像曝光行设置指令
	0x00, //读指令
	0x00, //2byte，范围0-1355 
	0x00,
};

/*获取固件日期指令*/
uint8_t cByteGetFirmwareDateCmd[] = {
	0x56,
	0x00, //序号
	0x12, //获取固件日期指令
	0x00, 
};

/*应答日期*/
uint8_t cByteRetFirmwareDateCmd[50] = {
	0x76,
	0x00, //序号
	0x12, //应答版本日期指令
	0x09, //固定长度
	//20240402  8byte
	0x32,0x30,0x32,0x34,0x30,0x34,0x30,0x32,
};



/*错误指令*/
uint8_t cByteError[] = {
	0x65,0x72,0x72,0x6F,0x72
};


#pragma pack(1)
typedef struct {
    uint8_t * pTxcmd;
    uint8_t * pRxcmd;
    // void (*do_fun)(uint32_t arg);
}CmdTbl_t;
#pragma pack()

CmdTbl_t sCmdTbl[] = {
  {cByteGetFirmwareDateCmd,cByteRetFirmwareDateCmd},
	{cByteSetExporuserTimeCmd,cByteRetSetExporuserTimeCmd},
	{cByteGetExporuserTimeCmd,cByteRetGetExporuserTimeCmd},
	{cByteISPImageFormatCmd,cByteRetISPImageFormatCmd},
	{cByteReadParamDetect,cByteRetReadParamDetect},
	{cByteSetParamDetect,cByteRetSetParamDetect},
	{cByteReadParamDefault,cByteRetReadParamISP},
	
	{cByteReadParamSerial,cByteRetReadParamSerial},
	{cByteResetParamISP,cByteRetResetParamISP},
	{cByteReadParamISP,cByteRetReadParamISP},
	{cByteSetParamISP,cByteRetSetParamISP},
	{cByteReadParamSnap,cByteRetReadParamSnap},
	{cByteSetParamSnap,cByteRetSetParamSnap},
	{cByteGetParam,cByteRetParam},
	{cByteSetColor,cByteRetSetColor},
	{cByteSetCrop,cByteRetSetCrop},
	{cByteReadCrop,cByteRetReadCrop},
	{cByteSetCompress,cByteRetSetCompress},
	
	{cBytePhotoContiue,cByteRetPhotoContiue},
	{cByteMonitorAttr,cByteRetMonitorAttr},
	{cByteOSDCtrl,cByteRetOSDCtrl},
	{cByteGetVersion,cByteRetVersion},
	{cBytePhotoYUV,cByteRetPhoto},
	{cBytePhoto,cByteRetPhoto},
	{cByteExtReadLength,cByteRetExtReadLength},
	{cByteReadLength,cByteRetReadLength},
	{cByteReadBuf,cByteRetReadBuf},
	{cByteCleanPhoto,cByteRetCleanPhoto},     
	{cByteReset,cByteRetReset},

	{cByteImageSize2304x1296,cBytePTC2MRetImageSize},
	
	{cByteDefBaud9600,cByteRetDefBaud},
	{cByteDefBaudExt921600,cByteRetDefBaud},/*增加921600波特率2*/
	
    {cByteChangeNum,cByteRetChangeNum},
	{cByteStandby,cByteRetStandby},
	{cByteNormal,cByteRetpowererror},
	{cByteCheckNum,cByteRetCheckNum},
	{cByteStartMD,cByteRetMD},
	{cByteStopMD,cByteRetMD},
	{cByteMDSensitivity,cByteRetMDSensitivity},
	{cByteLEDEnable,cByteRetLED},
};

extern int PortSend(uint8_t *data, int datalen);
extern int PortRecv(uint8_t *data);

/*
pSrc : 原始数据
pDst : 目标数据
返回值: 0-超时或数据错误  >0 接收到的数据长度
*/
int recv_compare(uint8_t * pSrc,uint8_t *pDst)
{
    int len = 0,ret = -1;
    int i = 0;
    len = PortRecv(pDst);
    if(len > 0) 
    {
        ret = memcmp(pDst,pSrc,3); /*只比较前3个字节*/
    }
    else {
        printf("Recv timeout!\n");
    }

    return (ret == 0) ? len : 0;
}

/*
获取固件版本
返回值：0-失败 1-成功
*/
uint8_t GetVersionFun(void)
{   uint8_t recv_buf[50] = "";
    int  ret = 0;

    PortSend(cByteGetVersion,sizeof(cByteGetVersion));
    ret = recv_compare(cByteRetVersion,recv_buf);
    if(ret > 0) {
        printf("CAMVER:%s\n",&recv_buf[5]); //第5字节开始是版本名称
    }

    return (ret > 0) ? 1 : 0;
}
/*返回值：0-失败 1-成功*/
uint8_t TakephotoFun(void)
{
    uint8_t recv_buf[50] = "";
    int  ret = 0;

    PortSend(cBytePhoto,sizeof(cBytePhoto));
    ret = recv_compare(cByteRetPhoto,recv_buf);
    
    printf("%s %s\n",__func__,((ret > 0) ? "success" : "fail"));
    return (ret > 0) ? 1 : 0;
}

/*返回值：图片长度*/
uint32_t GetPhotoLengthFun(void)
{
    uint8_t recv_buf[50] = "";
    int  ret = 0;
    uint32_t length = 0;
    
    PortSend(cByteReadLength,sizeof(cByteReadLength));
    ret = recv_compare(cByteRetReadLength,recv_buf);
    length = BIG_ENDIAN_UINT32(&recv_buf[5]);

    printf("%s %s\n",__func__,((ret > 0) ? "success" : "fail"));
    if(ret > 0) {printf("length:%d\n",length);}

    return length;
}

/*返回值：读到的数据长度*/
uint32_t ReadPhotoData(uint32_t Addr,uint32_t Nbytes,uint8_t * pJPGData)
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
    
    PortSend(cByteReadBuf,sizeof(cByteReadBuf));
    ret = recv_compare(cByteRetReadBuf,pJPGData);

    return (uint32_t)ret;
}

/*结构体按1字节对齐*/
#pragma pack(1)
typedef struct {
	uint8_t     bEnable;    //显示/关闭
	uint8_t     u8Line;     //行(0-3)
	uint8_t     u8FontType; //字体选择 0:16 1:24 2:32
	uint16_t    u16x;       //x坐标
	uint16_t    u16y;       //y坐标
	uint16_t    u16Color;   //字体颜色代码
	uint16_t    u16BkColor;   //背景颜色代码
	uint8_t     u8Length;   //显示内容长度
	uint8_t     u8Data[256];//限制最多显示80个汉字，160个ASCII字符
}OsdConf_t;
#pragma pack()


/*
pText: 要叠加到图片的文本，支持GB2312,ASCII
*/
int SetOSDText(OsdConf_t * pOsd)
{
    int ret  = 0;
    char path[30] = "";
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
    
    PortSend(cByteOSDCtrl, pOsd->u8Length + 16);
    ret = recv_compare(cByteRetOSDCtrl,recv_buf);

    return (uint32_t)ret;
}


/*
获取时间
pOut:      输出时间字符串
返回值:     时间字符串长度
*/
int GetRTC(char * pOut, int OutBufSize)
{
	struct timeval tv;
	struct tm      tm;
	size_t         len = 28;
	
	memset(&tv, 0, sizeof(tv));
	memset(&tm, 0, sizeof(tm));
	gettimeofday(&tv, NULL);
	localtime_r(&tv.tv_sec, &tm);
	strftime(pOut, OutBufSize, "%Y%m%d_%H%M%S", &tm);
	len = strlen(pOut);
	printf("%s\n", pOut);
	
	return len;
}

/*返回值：时间结构体指针*/
struct tm * GetRtc_tm(void)
{
    struct timeval tv;
	static struct tm      tm;
	
	memset(&tv, 0, sizeof(tv));
	memset(&tm, 0, sizeof(tm));
	gettimeofday(&tv, NULL);
	localtime_r(&tv.tv_sec, &tm);
    
    return &tm;
}

 OsdConf_t _sOsdConf[4] = {
            {
                .bEnable = 1,
                .u16BkColor = 0xfffe, //透明背景
                .u16Color = 0x007c,   //RGB555 蓝色文字
                .u16x = 0,
                .u16y = 0,
                .u8FontType = 0,
                .u8Line = 0,
								.u8Length = 24,
                .u8Data = "0123456789ABCDEFGabcdefg",
            },
            {1,0xffff,0x7c00,0,20,1,1,24,"0123456789ABCDEFGabcdefg"},
            {1,0xffff,0x43f0,0,48,2,2,19,"2024/12/11 20:00:30"},
            {1,0xffff,0x007c,0,80,2,3,0,NULL},
        };
        
        
/*
* 设置相机分辨率
* 0:失败，1:成功
*/
uint8_t SetResolutionFun(uint8_t *pResCmd)
{
    uint8_t recv_buf[50] = "";
    int ret = 0;
    PortSend(pResCmd,9);
    ret = recv_compare(cBytePTC2MRetImageSize,recv_buf);
    printf("%s %s\n",__func__,((ret > 0) ? "success" : "fail"));
    return (ret > 0) ? 1 : 0;
}


void CameraDemoApp(void)
{
    uint32_t length = 0;
    uint32_t AddrOff = 0, NByte = 20480, NTimes = 0, LastByte = 0;
    uint8_t  bCompleteRead = 1;
    uint8_t  bUseOsd = 1;
    uint8_t *pMemJPG = NULL;
    uint32_t rxcount = 0, total_size = 0;
    uint16_t i = 0;
    int fd = 0;
    int wret = 0;
    char time_buf[30] = "";
    char pFilepath[50];

    //=====生成时间戳文件名=====
    getPhotoFileName(pFilepath, sizeof(pFilepath));
    printf_time();
    printf("保存照片文件：%s\n", pFilepath);

    if(!GetVersionFun()) /*发一条命令确认通讯正常*/
    {
        return ;
    }
    
    SetResolutionFun(cByteImageSize2304x1296);
    
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
    
    //usleep(3000000); //3秒，给1080P足够编码时间

    length = GetPhotoLengthFun();
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

    pMemJPG = malloc(length + 10);
    fd = open(pFilepath,O_RDWR|O_CREAT,0777);
    if(pMemJPG && fd > 0)
    {
        for(i = 0; i < NTimes; )
        {
            if((i + 1) == NTimes) {NByte = LastByte;}
            rxcount = ReadPhotoData(AddrOff,NByte,pMemJPG);
            printf("rxcount:%d\n",rxcount);
			if(!rxcount)
			{
                //接收失败，释放资源，直接return！！不再执行后面代码
                close(fd);
                free(pMemJPG);
                pMemJPG = NULL;
                close(fd);
                remove(pFilepath); //删除残缺文件
                printf("图片接收超时失败！\n");
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

void MotionDetectCtrl(uint8_t Enable)
{
    uint8_t wkmode; /*侦测模式 detecetion mode*/
    struct sCmd_t{
        uint32_t AddrOff;
        uint32_t OnceByte;
    }__attribute__((packed));

    struct sCmd_t sCmd;
    int ret  = 0;
    uint8_t recv_buf[50] = "";

    memcpy((uint8_t *)&cByteReadBuf[6],(uint8_t *)&sCmd,sizeof(sCmd));
    
    PortSend(cByteMonitorAttr,sizeof(cByteMonitorAttr));
    ret = recv_compare(cByteRetMonitorAttr,recv_buf);

    return (uint32_t)ret;
}