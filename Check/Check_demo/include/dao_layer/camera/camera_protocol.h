#ifndef CAMERA_PROTOCOL_H
#define CAMERA_PROTOCOL_H

#include <stdint.h>

#define SWAP16(x) (((x >> 8) & 0xff) | ((x & 0xff ) << 8))   /*¸ßµÍ×Ö½Ú½»»»*/

#define SWAP32(x)		((((x) & 0x000000ff) << 24) | \
				 (((x) & 0x0000ff00) <<  8) | \
				 (((x) & 0x00ff0000) >>  8) | \
				 (((x) & 0xff000000) >> 24)) 

#define BIG_ENDIAN_UINT32(x) \
((uint32_t)(x)[0]<<24 | \
(uint32_t)(x)[1]<<16 | \
(uint32_t)(x)[2]<<8  | \
(uint32_t)(x)[3])

#define BIG_ENDIAN_UINT16(x) \
((uint16_t)((uint16_t)(x)[0]<<8 | (x)[1]))

extern uint8_t cByteGetVersion[4];
extern uint8_t cByteRetVersion[50];
extern uint8_t cBytePhoto[5];
extern uint8_t cByteImageSize2560x1920[9];
extern uint8_t cByteImageSize2560x1440[9];
extern uint8_t cByteImageSize1920x1080[9];
extern uint8_t cByteOSDCtrl[512];
extern uint8_t cBytePTC2MRetImageSize[5];
extern uint8_t cByteRetReadLength[9];
extern uint8_t cByteReadBuf[16];
extern uint8_t cByteRetReadBuf[5];
extern uint8_t cByteReadLength[5];
extern uint8_t cByteRetOSDCtrl[10];
extern uint8_t cByteRetPhoto[5];
extern uint8_t cBytePhotoYUV[5];


#pragma pack(1)
typedef struct 
{
    uint8_t     bEnable;
    uint8_t     u8Line;
    uint8_t     u8FontType;

    uint16_t    u16x;
    uint16_t    u16y;

    uint16_t    u16Color;
    uint16_t    u16BkColor;

    uint8_t     u8Length;
    uint8_t     u8Data[256];

}OsdConf_t;
#pragma pack()

#endif