#ifndef CAMERA_SERIAL_H
#define CAMERA_SERIAL_H

#include <stdint.h>

#include "dao_layer/camera/camera_config.h"

class CameraSerial
{

public:

    CameraSerial();

    int PortOpen(pportinfo_t pportinfo);

    int PortSend(uint8_t *data,int datalen);

    int PortRecv(uint8_t *outdata);

    int recv_compare(uint8_t * pSrc,uint8_t *pDst);

private:

    int cdcFd;

    int PortSet(int fdcom,const pportinfo_t pportinfo);

    int convbaud(unsigned long int baudrate);

    const char *get_ptty(pportinfo_t pportinfo);
    

};

#endif