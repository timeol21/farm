#ifndef CAMERA_H
#define CAMERA_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>              // open   
#include <sys/types.h>          // pid_t   
#include <sys/select.h>
#include <termios.h>          //termios, tcgetattr(), tcsetattr()     
#include <sys/ioctl.h> 
#include <stddef.h>
#include <stdlib.h>
#include <sys/stat.h>  //stat
#include <unistd.h>  //O_RDWR
#include <time.h>
#include <sys/time.h>

#include "dao_layer/camera/camera_config.h"

#include "dao_layer/camera/camera_serial.h"

#include "dao_layer/camera/camera_image.h"

#include "dao_layer/camera/camera_protocol.h"

class Camera
{

public:

    Camera();

    void CameraDemoApp();

    uint8_t GetVersionFun();

    uint8_t SetResolutionFun(uint8_t *pResCmd);

    int SetOSDText(OsdConf_t *pOsd);

    uint8_t TakephotoFun();
    
    int printf_time(void);

private:

    CameraSerial serial;

    CameraImage image;

    OsdConf_t _sOsdConf[4] =
    {
        {
            1,              // bEnable
            0,              // u8Line
            0,              // u8FontType
            0,              // u16x
            0,              // u16y
            0x007c,         // u16Color
            0xfffe,         // u16BkColor
            24,             // u8Length
            "0123456789ABCDEFGabcdefg"
        },
    
        {
            1,
            1,
            1,
            0,
            20,
            0x7c00,
            0xffff,
            24,
            "0123456789ABCDEFGabcdefg"
        },
    
        {
            1,
            2,
            2,
            0,
            48,
            0x43f0,
            0xffff,
            19,
            "2024/12/11 20:00:30"
        },
        {
            1,
            3,
            2,
            0,
            80,
            0x007c,
            0xffff,
            0,
            ""
        }
    };

};

#endif