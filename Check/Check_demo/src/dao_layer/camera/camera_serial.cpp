#include "dao_layer/camera/camera_serial.h"
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <stdio.h>
#include <string.h>

CameraSerial::CameraSerial()
    :
    cdcFd(-1)
{

}
const char *CameraSerial::get_ptty(pportinfo_t pportinfo)
{  
    const char *ptty = nullptr;
    
    switch(pportinfo->tty){  
        case '0':  
            ptty = TTY_DEV"0";  
            break;
        case '1':  
            ptty = TTY_DEV"1";  
            break;  
        case '2':  
            ptty = TTY_DEV"2";  
            break;  
        case '3':  
            ptty = TTY_DEV"3";  
            break; 
        case '4':  
            ptty = TTY_DEV"4";  
            break; 
        case '5':  
            ptty = TTY_DEV"5";  
            break; 
        default:
            ptty = TTY_DEV"0";
            break;
    }  
    return(ptty);  
}  

int CameraSerial::convbaud(unsigned long int baudrate)  
{  
    switch(baudrate){  
        case 2400:  
            return B2400;
        case 4800:  
            return B4800;
        case 9600:  
            return B9600;
        case 19200:  
            return B19200;
        case 38400:  
            return B38400;
        case 57600:  
            return B57600;
        case 115200:  
            return B115200;  
        case 230400:
            return B230400;
        case 460800:
            return B460800;
        case 921600:
            return B921600;
        case 1000000:
            return B1000000;
        case 1500000:
            return B1500000;    
        case 2000000:
            return B2000000;            
        default:  
            return B115200;
    }
} 

int CameraSerial::recv_compare(uint8_t * pSrc,uint8_t *pDst)
{
    int len = 0,ret = -1;
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

int CameraSerial::PortSend(uint8_t *data, int datalen)  
{  
    int j = 0;
    int len = 0;  
    int fdcom = cdcFd;
    
    if(cdcFd < 0)
    {
        printf("PortSend error: invalid fd=%d\n", cdcFd);
        return -1;
    }


    if(data == NULL)
    {
        printf("PortSend error: data is NULL\n");
        return -1;
    }
    
    printf("PortSend fd=%d len=%d\n", cdcFd, datalen);
    
    // lseek(fdcom,0,SEEK_SET);
    len = write(fdcom, data, datalen);  //实际写入的长度  
    
    if(len < 0)
    {
        perror("write error");
        return -1;
    }
     
    //printf("len:%d,0x%x\n",len,len);
    #if 1 /*打印测试*/
    printf("Tx:%d\n",len);
    for(j = 0; j < len; j++) {
        printf("%02x ",data[j]);
    }
    printf("\n");
    #endif
    if(len == datalen){  
        return (len);  
    }  
    else{  
        tcflush(fdcom, TCOFLUSH);  
        return -1;  
    }  
}  

int CameraSerial::PortOpen(pportinfo_t pportinfo)  
{  
    int fdcom;  //串口文件描述符   
    const char *ptty;
    int BaudBak = 0;
    
    //HalUartx_Init(UART2_REG_BASE,pportinfo->baudrate); /*l-硬件初始化*/ 
    ptty = get_ptty(pportinfo);  

    // fdcom = open(ptty, O_RDWR | O_NOCTTY | O_NONBLOCK);
    fdcom = open(ptty, O_RDWR | O_NOCTTY | O_NONBLOCK); 
    if(fdcom >= 0){
        BaudBak = pportinfo->baudrate; //备份存储的波特率
        // pportinfo->baudrate = 115200; //设置默认波特率
        PortSet(fdcom,pportinfo);
        pportinfo->baudrate = BaudBak; //还原原始波特率
        // ioctl(fdcom, 0x101, pportinfo->baudrate); //配置波特率，其他默认
        // ioctl(fdcom, 0x101, 115200);
        // ioctl(fdcom, 0x102, 1); //rx dma  /*使用中断方式在数据接收数据量大时，内核会直接崩溃，改用DMA方式*/
        // ioctl(fdcom, 0x104, 0); //no block
        //ioctl(fdcom, 0x103,1);  // tx dma
        
        cdcFd = fdcom;
        printf("CameraSerial cdcFd=%d\n",cdcFd);
    }
    
    return (fdcom);  
} 

int CameraSerial::PortRecv(uint8_t *outdata)  
{
    int TimesCnt = 0;
    int readlen = 0, readlenbak = 0,fs_sel = 0;
    int ReadCnt = 0;
    uint8_t buffer[32] = "";
    fd_set  fs_read;  
    struct timeval tv_timeout;  
    int fd_com = cdcFd;

    
    /*注意超时时间，设置太长相应会变慢，波特率越高，超时要短点*/

    tv_timeout.tv_sec = 1;
    tv_timeout.tv_usec = 0; //1ms
    
    while(1)
    {
        FD_ZERO(&fs_read);  
        FD_SET(fd_com, &fs_read);  
        
        fs_sel = select(fd_com+1, &fs_read, NULL, NULL, &tv_timeout);
        if(fs_sel && FD_ISSET(fd_com, &fs_read)){ 
            
            ReadCnt = read(fd_com, outdata + readlen, sizeof(buffer));  //一次读32字节 
            tv_timeout.tv_sec = 0;
            tv_timeout.tv_usec = 0;
            
            // memcpy(outdata + readlen,buffer,ReadCnt);
            readlen += ReadCnt;
            
            #if 0 /*打印测试*/
            printf("Rx:%d\n",ReadCnt);
            for(j = 0; j < ReadCnt; j++) {
                printf("%02x ",buffer[j]);
            }
            printf("\n");
            #endif
        }  
        else{  
            // printf("sel:%d\n",fs_sel);
            // break;
            if(readlen != readlenbak) { /*长度有变化，数据还没接收完，超时重新开始*/
                readlenbak = readlen;
                TimesCnt = 0;
                tv_timeout.tv_sec = 0;
                tv_timeout.tv_usec = 0;
            }
            else { /*长度没在变化设置100ms超时退出*/
                tv_timeout.tv_sec = 0;
                tv_timeout.tv_usec = 1000;
                if(++TimesCnt >= 10) { /*10ms溢出*/
                    printf("readlen:%d\n",readlen);
                    break;
                }
            }
            continue;
        } 
    }
    
    return (readlen);  
} 

int CameraSerial::PortSet(int fdcom, const pportinfo_t pportinfo)  
{  
    #if 1
    struct termios termios_old, termios_new;  
    int     baudrate, tmp;  
    char    databit, stopbit, parity, fctl;  
    
    bzero(&termios_old, sizeof(termios_old));  
    bzero(&termios_new, sizeof(termios_new));  
    cfmakeraw(&termios_new);  
    tcgetattr(fdcom, &termios_old);         //get the serial port attributions   
    /*------------设置端口属性----------------*/  
    //baudrates   
    baudrate = convbaud(pportinfo -> baudrate);  
    cfsetispeed(&termios_new, baudrate);        //填入串口输入端的波特率   
    cfsetospeed(&termios_new, baudrate);        //填入串口输出端的波特率   
    termios_new.c_cflag |= CLOCAL;          //控制模式，保证程序不会成为端口的占有者   
    termios_new.c_cflag |= CREAD;           //控制模式，使能端口读取输入的数据   
    
    // 控制模式，flow control   
    fctl = pportinfo-> fctl;  
    switch(fctl){  
        case '0':{  
            termios_new.c_cflag &= ~CRTSCTS;        //no flow control   
        }break;  
        case '1':{  
            termios_new.c_cflag |= CRTSCTS;         //hardware flow control   
        }break;  
        case '2':{  
            termios_new.c_iflag |= IXON | IXOFF |IXANY; //software flow control   
        }break;  
    }  
    
    //控制模式，data bits   
    termios_new.c_cflag &= ~CSIZE;      //控制模式，屏蔽字符大小位   
    databit = pportinfo -> databit;  
    switch(databit){  
        case '5':  
            termios_new.c_cflag |= CS5;
            break;  
        case '6':  
            termios_new.c_cflag |= CS6;
            break;  
        case '7':  
            termios_new.c_cflag |= CS7;
            break;  
        default:  
            termios_new.c_cflag |= CS8;
            break;  
    }  
    
    //控制模式 parity check   
    parity = pportinfo -> parity;  
    switch(parity){  
        case '0':{  
            termios_new.c_cflag &= ~PARENB;     //no parity check   
        }break;  
        case '1':{  
            termios_new.c_cflag |= PARENB;      //odd check   
            termios_new.c_cflag &= ~PARODD;  
        }break;  
        case '2':{  
            termios_new.c_cflag |= PARENB;      //even check   
            termios_new.c_cflag |= PARODD;  
        }break;  
    }  
    
    //控制模式，stop bits   
    stopbit = pportinfo -> stopbit;  
    if(stopbit == '2'){  
        termios_new.c_cflag |= CSTOPB;  //2 stop bits   
    }  
    else{  
        termios_new.c_cflag &= ~CSTOPB; //1 stop bits   
    }  
    
    //other attributions default   
    termios_new.c_oflag &= ~OPOST;          //输出模式，原始数据输出   
    termios_new.c_lflag &= ~ICANON;         //立即输出
    /*
    c_cc[VMIN] = 0 时，调用read 才不会阻塞
    */
    termios_new.c_cc[VMIN]  = 0;//1;            //控制字符, 所要读取字符的最小数量   
    termios_new.c_cc[VTIME] = 0;//1;            //控制字符, 读取第一个字符的等待时间    unit: (1/10)second   
    
    tcflush(fdcom, TCIFLUSH);               //溢出的数据可以接收，但不读   
    tmp = tcsetattr(fdcom, TCSANOW, &termios_new);  //设置新属性，TCSANOW：所有改变立即生效    tcgetattr(fdcom, &termios_old);   
    return(tmp);
    #endif
    return 0;  
}  
