CXX = g++
CXXFLAGS = -Wall -std=c++17 -I. -I./DataAccess -I./Entity -I./Topology -I./Business
LDFLAGS = -lpthread

# 完整的项目编译列表
SRCS = main.cpp \
       DataAccess/ChannelManager.cpp \
       DataAccess/SerialChannel.cpp \
       Business/ComponentRegistry.cpp \
       Entity/WaterLevelSensor.cpp \
       Entity/SmokeDetector.cpp \
       Entity/InfraredSensor.cpp \
       Entity/ValveController.cpp

OBJS = $(SRCS:.cpp=.o)
TARGET = box_system

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)