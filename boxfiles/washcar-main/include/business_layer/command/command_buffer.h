#pragma once
#include "command_status.h"
#include <queue>

class CommandBuffer {
public:
	// 获取单例实例
	static CommandBuffer& getInstance();
	// 禁用拷贝/移动
	CommandBuffer(const CommandBuffer&) = delete;
	CommandBuffer& operator=(const CommandBuffer&) = delete;
	CommandBuffer(CommandBuffer&&) = delete;
	CommandBuffer& operator=(CommandBuffer&&) = delete;

	void addCommandStatus(const CommandStatus& commandStatus);
	bool hasNext() const;
	CommandStatus getNext();
private:
	CommandBuffer() = default;
	~CommandBuffer() = default;

	CommandQueue _commandQueue;
};
