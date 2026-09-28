#include "dao_layer/camera/camera_config.h"

portinfo_t sPortInfo =
{  
		'0',                            // print prompt after receiving   
		921600,                         // baudrate: 115200   
		'8',                            // databit: 8   
		'0',                            // debug: off   
		'0',                            // echo: off   
		'0',                            // flow control: software   
		'0',                            // default tty: COM0   
		'0',                            // parity: none   
		'1',                            // stopbit: 1   
		 0 ,                         // reserved   
		1024                              //串口一次往设备节点写入的长度，波特率越小，值应越小
};