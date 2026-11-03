#pragma once
#include <cstdint>
#include <string>
#include <vector>

// 业务结果类型(独立枚举: DetectionAlarm 先于 DetectionResult 定义, 避免循环依赖)
enum class DetectionResultType {
    LICENSE_PLATE,    // 车牌
    DOOR_OPEN,        // 车门打开
    WINDOW_OPEN,      // 车窗打开
    ROOF_OBSTACLE,    // 车顶异物
    PERSON_DETECTED,  // 区域有人
    NO_CAR,           // 无车
    CAR_PRESENT,      // 车存在性(业务规则"车"模型)
    MOTORCYCLE,       // 摩托车
    BICYCLE           // 自行车
};

// ============ 待报项(检测服务判定后写进结果, 大厅读后上报) ============
struct DetectionAlarm {
    enum class Type {
        PERSON,       // 检测到人 -> 语音告警(空实现, 无硬件)
        ABNORMAL,     // 车顶/车门/车窗异常 -> 告警消息 + 异常图片
    };
    Type                type;
    DetectionResultType itemType;    // ABNORMAL 时的具体异常项
    std::string         imagePath;   // ABNORMAL 时异常图片路径(<storageRoot>/images/)
    int64_t             timestamp;   // 事件发生时间(毫秒)
};

class DetectionResult {
public:
    using Type = DetectionResultType;   // 兼容旧写法 DetectionResult::Type

    struct Item {
        Type type;
        bool detected;
        std::string value;
        float confidence;
        int64_t timestamp;
        std::string imagePath;   // 命中项留证图片(<storageRoot>/images/), 非空表示已落盘
        float box[4] = {0, 0, 0, 0};   // 命中位置(原图坐标 x1,y1,x2,y2); 配 imagePath 一起才还原得出上下文
    };

    DetectionResult();

    void addItem(Type type, bool detected, const std::string& value = "",
                 float confidence = 0.0f, int64_t timestamp = 0, const std::string& imagePath = "");

    static std::string itemTypeName(Type t);   // 告警上报用可读名(如 "roof_obstacle")

    bool isValid(int validDurationMs = 3000) const;
    bool hasItem(Type type) const;
    std::string getItemValue(Type type) const;
    std::string getItemImagePath(Type type) const;

    std::vector<Item> items;
    std::vector<DetectionAlarm> alarms;   // 待报项(检测服务判定生成, 大厅上报后 consumeAlarms 清空)
    int64_t frameTimestamp;
};

// ============ 业务类别名 <-> 枚举(配置里写字符串, 检测服务构造时解析) ============
bool typeFromString(const std::string& s, DetectionResultType& out);
std::string typeToString(DetectionResultType t);
