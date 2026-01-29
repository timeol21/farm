CXX = g++
CXXFLAGS = -std=c++11 -Wall -O2
LDFLAGS = -lpthread

TARGET = box_pro
SRCS = main.cpp SerialManager.cpp Sensors.cpp Controllers.cpp DeviceFactory.cpp
OBJS = $(SRCS:.cpp=.o)

$(TARGET): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f *.o $(TARGET)