#ifndef __CAMERA_CMD_H__
#define __CAMERA_CMD_H__

#include <stdint.h>
int printf_time(void);
int getPhotoFileName(char *filePath, int bufSize);
void GetVersionFun(void);
uint8_t TakephotoFun(void);
uint32_t GetPhotoLengthFun(void);
uint32_t ReadPhotoData(uint32_t Addr,uint32_t Nbytes,uint8_t * pJPGData);
uint8_t SetResolutionFun(uint8_t *pResCmd);
void CameraDemoApp(void);
#endif

