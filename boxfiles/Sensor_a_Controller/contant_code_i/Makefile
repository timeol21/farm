# 编译器和参数
CXX = g++
CXXFLAGS = -std=c++11 -Wall

# 目标程序名
TARGET = box_system

# 所有的源文件（列出本项目所有的 cpp 文件）
SRCS = main.cpp Device.cpp SerialManager.cpp Sensors.cpp Controllers.cpp DeviceFactory.cpp

# 生成的目标文件（将 .cpp 替换成 .o）
OBJS = $(SRCS:.cpp=.o)

# 默认规则：编译整个项目
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(TARGET) -lpthread

# 编译每个 .cpp 到 .o 的规则
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 清理
clean:
	rm -f $(OBJS) $(TARGET)

# 一键运行
run: $(TARGET)
	sudo ./$(TARGET)