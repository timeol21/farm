#include "data_layer/command/command_object.h"

CommandEntity::CommandEntity(const Command& cmd)
    : cmdId(cmd.getCmdId()),
      deviceId(cmd.getDeviceId()),
      type(cmd.getCmdType()),
      state(cmd.getCmdState()),
      content(cmd.getCmdContent()),
      createTime(cmd.getCreateTime()),
      updateTime(0)
{
}

CommandEntity CommandEntity::createEntity(const Command& cmd){
    // 在这里给成员赋值
    CommandEntity entity(cmd); // 调用构造函数
    return entity;
}