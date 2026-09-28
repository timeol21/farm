CROSS_COMPILE :=

CC = $(CROSS_COMPILE)gcc
CPLUSPLUS = $(CROSS_COMPILE)g++
LD = $(CROSS_COMPILE)ld
AR = $(CROSS_COMPILE)ar cr
STRIP = $(CROSS_COMPILE)strip

CFLAGS = $(INCLUDES) -O2 -Wall

LDFLAG += -Wl,-gc-sections

LIBS :=

SAMPLES = camcdc

all:	$(SAMPLES)

camcdc:  serialcam.o camera_cmd.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt -ldl
	$(STRIP) $@

clean:
	rm -f *.o *~

distclean: clean
	rm -f $(SAMPLES)

