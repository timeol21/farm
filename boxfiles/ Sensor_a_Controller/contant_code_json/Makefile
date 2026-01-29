CXX = g++
CXXFLAGS = -std=c++11 -Wall

TARGET = box_system
SRCS = main.cpp Device.cpp SerialManager.cpp Sensors.cpp Controllers.cpp DeviceFactory.cpp
OBJS = $(SRCS:.cpp=.o)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(TARGET) -lpthread

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

run: $(TARGET)
	sudo ./$(TARGET)