#pragma once

// 上报类别 -> MQTT endpoint 的唯一映射。
//
// 业务层只说 ReportKind，endpoint 字符串只在这里出现。
// 云端要改 topic 命名，改这一个函数即可，业务代码不动。

#include "common/message.h"

namespace comm {

inline const char* endpointFor(ReportKind kind) {
    switch (kind) {
    case ReportKind::MONITOR:       return "monitor";
    case ReportKind::PUSHALARM:     return "pushalarm";
    case ReportKind::READDATA:      return "readdata";
    case ReportKind::COMMAND_REPLY: return "commandreply";
    }
    return "monitor";
}

}  // namespace comm
