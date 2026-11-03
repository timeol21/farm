#ifndef CAMERA_UTILS_H
#define CAMERA_UTILS_H

#include <memory>
#include <vector>
#include <cstdint>

enum class CameraOnlineStatus {
    OFFLINE,
    ONLINE
};

struct FrameData {
    std::shared_ptr<uint8_t> data;
    int width = 0;
    int height = 0;
    int64_t timestamp_ = 0;

    std::vector<uint8_t> toVector() const {
        if (!data || width <= 0 || height <= 0) {
            return {};
        }
        return std::vector<uint8_t>(data.get(), data.get() + width * height * 3);
    }
};

#endif
