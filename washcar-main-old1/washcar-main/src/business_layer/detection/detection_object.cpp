#include "business_layer/detection/detection_object.h"
#include <chrono>

// ============ DetectionResult ============
DetectionResult::DetectionResult() : frameTimestamp(0) {}

void DetectionResult::addItem(Type type, bool detected, const std::string& value,
                              float confidence, int64_t timestamp, const std::string& imagePath) {
    items.push_back({type, detected, value, confidence, timestamp, imagePath});
}

std::string DetectionResult::itemTypeName(Type t) {
    switch (t) {
        case Type::CAR_PRESENT:      return "car";
        case Type::PERSON_DETECTED:  return "person";
        case Type::ROOF_OBSTACLE:    return "roof_obstacle";
        case Type::DOOR_OPEN:        return "door_open";
        case Type::WINDOW_OPEN:      return "window_open";
        case Type::LICENSE_PLATE:    return "license_plate";
        case Type::MOTORCYCLE:       return "motorcycle";
        case Type::BICYCLE:          return "bicycle";
        default:                     return "no_car";
    }
}

bool DetectionResult::isValid(int validDurationMs) const {
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    return (now - frameTimestamp) < validDurationMs;
}

bool DetectionResult::hasItem(Type type) const {
    for (const auto& item : items) {
        if (item.type == type) {
            return item.detected;
        }
    }
    return false;
}

std::string DetectionResult::getItemValue(Type type) const {
    for (const auto& item : items) {
        if (item.type == type) {
            return item.value;
        }
    }
    return "";
}

std::string DetectionResult::getItemImagePath(Type type) const {
    for (const auto& item : items) {
        if (item.type == type) {
            return item.imagePath;
        }
    }
    return "";
}

// ============ 业务类别名 <-> 枚举 ============
bool typeFromString(const std::string& s, DetectionResultType& out) {
    if (s == "LICENSE_PLATE")     { out = DetectionResultType::LICENSE_PLATE; }
    else if (s == "DOOR_OPEN")    { out = DetectionResultType::DOOR_OPEN; }
    else if (s == "WINDOW_OPEN")  { out = DetectionResultType::WINDOW_OPEN; }
    else if (s == "ROOF_OBSTACLE"){ out = DetectionResultType::ROOF_OBSTACLE; }
    else if (s == "PERSON_DETECTED") { out = DetectionResultType::PERSON_DETECTED; }
    else if (s == "NO_CAR")       { out = DetectionResultType::NO_CAR; }
    else if (s == "CAR_PRESENT")  { out = DetectionResultType::CAR_PRESENT; }
    else if (s == "MOTORCYCLE")   { out = DetectionResultType::MOTORCYCLE; }
    else if (s == "BICYCLE")      { out = DetectionResultType::BICYCLE; }
    else { return false; }
    return true;
}

std::string typeToString(DetectionResultType t) {
    switch (t) {
        case DetectionResultType::LICENSE_PLATE:    return "LICENSE_PLATE";
        case DetectionResultType::DOOR_OPEN:        return "DOOR_OPEN";
        case DetectionResultType::WINDOW_OPEN:      return "WINDOW_OPEN";
        case DetectionResultType::ROOF_OBSTACLE:    return "ROOF_OBSTACLE";
        case DetectionResultType::PERSON_DETECTED:  return "PERSON_DETECTED";
        case DetectionResultType::NO_CAR:           return "NO_CAR";
        case DetectionResultType::CAR_PRESENT:      return "CAR_PRESENT";
        case DetectionResultType::MOTORCYCLE:       return "MOTORCYCLE";
        case DetectionResultType::BICYCLE:          return "BICYCLE";
        default:                                    return "";
    }
}
