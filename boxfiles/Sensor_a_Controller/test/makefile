# ************************* 配置项（根据你的实际情况修改这3处即可）*************************
# 1. 可执行文件名称（自定义，比如PLC_Control、Solenoid_Control）
TARGET = PLC_Solenoid
# 2. 你的串口路径（和config.json里的serial_port一致，多个用空格分隔）
SERIAL_PORTS = /dev/ttyS1 /dev/ttyS3 /dev/ttyS4
# 3. 源文件列表（所有.cpp文件，缺一不可，按你的实际文件写）
SRCS = main.cpp Init.cpp Valve.cpp PLCConfig.cpp
# *****************************************************************************************

# 编译器及编译选项（C++17标准，适配你的编译器，加-Wall显示警告便于调试）
CXX = g++
CXXFLAGS = -std=c++17 -Wall
# 链接选项（保留-lpthread，兼容原代码）
LDFLAGS = -lpthread
# 目标文件（自动生成，无需修改）
OBJS = $(SRCS:.cpp=.o)

# 默认目标：make 一键编译
all: $(TARGET)

# 编译规则：将.cpp编译为.o，再链接为可执行文件
$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET) $(LDFLAGS)
	@echo "✅ 编译完成！可执行文件：./$(TARGET)"

# 编译.cpp为.o的通用规则
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 一键运行：make run （先编译→再赋串口权限→最后运行，全程一键）
run: all
	@echo "🔑 正在赋予串口权限..."
	sudo chmod 666 $(SERIAL_PORTS)
	@echo "🚀 正在运行程序..."
	./$(TARGET)

# 清理编译产物：make clean （删除.o目标文件和可执行文件）
clean:
	rm -rf $(OBJS) $(TARGET)
	@echo "🧹 清理完成！已删除所有编译产物"

# 伪目标（避免和同名文件冲突）
.PHONY: all run clean