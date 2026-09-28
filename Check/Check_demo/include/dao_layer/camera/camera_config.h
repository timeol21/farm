#ifndef CAMERA_CONFIG_H
#define CAMERA_CONFIG_H

#include <stdint.h>

// #define TTY_DEV "/dev/ttyUSB" 
// #define TTY_DEV "/dev/ttyCH341USB"
#define TTY_DEV "/dev/ttyACM" 
// #define TTY_DEV "/dev/ttyS" 

typedef struct{
 char prompt;  //prompt after reciving data
 int  baudrate;  //baudrate
 char databit;  //data bits, 5, 6, 7, 8
 char  debug;  //debug mode, 0: none, 1: debug
 char  echo;   //echo mode, 0: none, 1: echo
 char fctl;   //flow control, 0: none, 1: hardware, 2: software
 char  tty;   //tty: 0, 1, 2, 3, 4, 5, 6, 7
 char parity;  //parity 0: none, 1: odd, 2: even
 char stopbit;  //stop bits, 1, 2
 const int reserved; //reserved, must be zero
 int Tx_count;
}portinfo_t;

typedef portinfo_t * pportinfo_t;

extern portinfo_t sPortInfo;

#endif