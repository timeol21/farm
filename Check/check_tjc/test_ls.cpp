#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <cstdio>
#include <vector>
#include <cstring>

int openUart(const char* dev)
{
    int fd = open(dev, O_RDWR | O_NOCTTY);
    struct termios t;
    tcgetattr(fd, &t);
    cfmakeraw(&t);
    t.c_cflag = B115200 | CS8 | CLOCAL | CREAD;
    t.c_iflag = 0;
    t.c_oflag = 0;
    t.c_lflag = 0;
    t.c_cc[VMIN]=0;
    t.c_cc[VTIME]=10;
    tcsetattr(fd,TCSANOW,&t);
    return fd;
}
void sendCmd(int fd, const char* cmd)
{
    write(fd, cmd, strlen(cmd));
    unsigned char end[] = {0xFF,0xFF,0xFF};
    write(fd, end, 3);
    usleep(50000);
}
int main()
{
    int fd = openUart("/dev/ttyUSB0");
    sendCmd(fd, "ls ram");
    unsigned char buf[256];
    int n = read(fd, buf, sizeof(buf)-1);
    printf("screen reply: ");
    for(int i=0;i<n;i++) printf("%02X ", buf[i]);
    printf("\n");
    close(fd);
    return 0;
}
