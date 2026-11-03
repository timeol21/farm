#include "data_layer/command/command_object.h"

#include <chrono>

CommandEntity::CommandEntity(Command::Type type, const std::string& deviceId)
    : cmdId(Command::createId()),
      deviceId(deviceId),
      type(type),
      createTime(static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch()).count())),
      updateTime(0)
{
}
