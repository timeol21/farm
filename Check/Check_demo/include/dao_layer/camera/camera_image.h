#ifndef CAMERA_IMAGE_H
#define CAMERA_IMAGE_H

#include <stdint.h>
#include "dao_layer/camera/camera_serial.h"
#include "dao_layer/camera/camera_protocol.h"

class CameraImage
{

public:
    CameraImage(CameraSerial &serial);

    int getPhotoFileName(char *filePath,int bufSize);

    uint32_t GetPhotoLengthFun();

    uint32_t ReadPhotoData(uint32_t Addr,uint32_t Nbytes,uint8_t *pJPGData);
    
    struct tm * GetRtc_tm(void);
    
private:

    CameraSerial &serial;

};

#endif