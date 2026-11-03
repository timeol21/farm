#pragma once

#include "common/config/config_object.h"
#include <string>

// 洗车机软元件表的默认值。跟 default_config.cpp 同一套路:
// config.json 里没配这一段时,用这里的内置表兜底。
// 运行时真正生效的那份由 ConfigManager::machine() 按 box.boxType 选。
//
// 这里只放"这台机器长什么样"(点位、名字、上报通道、可调服务、故障位),
// 不放"怎么判"—— 判定规则在洗车机状态机里按机型写成子类,
// 见 business_layer/washcar/washcar_state_machine.h。
namespace default_point {

// 按洗车机厂商取默认软元件表(boxType 非 "yiGongLi" 一律回退海天)。
const washcar::cfg::MachinePointTable& getPointTable(const std::string& boxType);

}  // namespace default_point
