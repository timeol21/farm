CROSS_COMPILE :=

CC = $(CROSS_COMPILE)gcc
CPLUSPLUS = $(CROSS_COMPILE)g++

LD = $(CROSS_COMPILE)ld
AR = $(CROSS_COMPILE)ar cr
STRIP = $(CROSS_COMPILE)strip


INCLUDES =

CXXFLAGS = $(INCLUDES) -O2 -Wall -std=c++11

LDFLAG += -Wl,--gc-sections

LIBS :=


SAMPLES = camcdc


all: $(SAMPLES)


camcdc: cam.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt -ldl
	$(STRIP) $@


cam.o: cam.cpp
	$(CPLUSPLUS) $(CXXFLAGS) -c $< -o $@


clean:
	rm -f *.o *~


distclean: clean
	rm -f $(SAMPLES)