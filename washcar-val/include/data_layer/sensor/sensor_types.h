#ifndef SENSOR_TYPES_H
#define SENSOR_TYPES_H

enum class TempHumidStatus {
    NORMAL = 0,
    ABNORMAL = -1,
    OFFLINE = 1
};

enum class InfraredStatus {
    NORMAL = 0,
    TRIGGER = 1,
    UNKNOW = -1
};

enum class SmokeDetectorStatus {
    NORMAL = 0,
    ALARM = 1,
    UNKNOW = -1
};

enum class WaterLevelSensorStatus {
    NORMAL = 0,
    ABNORMAL = -1,
    UNKNOW = -2,
    OFFLINE = 1
};

#endif