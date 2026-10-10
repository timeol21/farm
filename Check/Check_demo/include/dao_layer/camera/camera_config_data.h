#ifndef CAMERA_CONFIG_DATA_H
#define CAMERA_CONFIG_DATA_H


#include <string>

uint8_t* getResolutionCommand(
    const std::string& name
);

struct SerialConfig
{

    char tty;

    int baudrate;

    char databit;

    char debug;

    char echo;

    char fctl;

    char parity;

    char stopbit;

};


struct ImageConfig
{

    std::string resolution;

    std::string savePath;

};


struct OsdConfig
{

    bool enable;

};


struct CameraConfig
{

    int id;

    std::string type;


    SerialConfig serial;

    ImageConfig image;

    OsdConfig osd;

};


#endif