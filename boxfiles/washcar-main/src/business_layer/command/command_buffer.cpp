#include "business_layer/command/command_buffer.h"

CommandBuffer& CommandBuffer::getInstance() {
	static CommandBuffer instance;
	return instance;
}

void CommandBuffer::addCommandStatus(const CommandStatus& commandStatus) {
	_commandQueue.addCommandStatus(commandStatus);
}

bool CommandBuffer::hasNext() const {
	return _commandQueue.hasNext();
}

CommandStatus CommandBuffer::getNext() {
	return _commandQueue.getNext();
}