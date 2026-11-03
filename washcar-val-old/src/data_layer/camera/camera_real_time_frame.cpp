#include "data_layer/camera/camera_real_time_frame.h"
#include "common/log/log_manager.h"

#include <fstream>
#include <vector>
#include <cstring>

#pragma pack(push, 1)
struct BMPHeader {
    char signature[2] = {'B', 'M'};
    uint32_t fileSize;
    uint32_t reserved = 0;
    uint32_t dataOffset = 54;

    uint32_t headerSize = 40;
    int32_t width;
    int32_t height;
    uint16_t planes = 1;
    uint16_t bitsPerPixel = 24;
    uint32_t compression = 0;
    uint32_t imageSize;
    int32_t xPixelsPerMeter = 0;
    int32_t yPixelsPerMeter = 0;
    uint32_t colorsUsed = 0;
    uint32_t colorsImportant = 0;
};
#pragma pack(pop)

CameraRealTimeFrame::CameraRealTimeFrame(const FrameData& frameData)
    : frameData(frameData) {
}

bool CameraRealTimeFrame::saveAsBmp(const std::string& filename) {
    if (!isValid()) {
        LOG_ERROR("CameraRealTimeFrame: Cannot save, frame is invalid");
        return false;
    }

    std::vector<uint8_t> rgbData = frameData.toVector();
    if (rgbData.empty()) {
        LOG_ERROR("CameraRealTimeFrame: Failed to get RGB data");
        return false;
    }

    BMPHeader header;
    header.width = frameData.width;
    header.height = frameData.height;
    header.imageSize = rgbData.size();
    header.fileSize = sizeof(BMPHeader) + rgbData.size();

    std::ofstream outFile(filename, std::ios::binary);
    if (!outFile) {
        LOG_ERROR("CameraRealTimeFrame: could not open output file: " + filename);
        return false;
    }

    outFile.write(reinterpret_cast<const char*>(&header), sizeof(header));
    outFile.write(reinterpret_cast<const char*>(rgbData.data()), rgbData.size());
    outFile.close();

    LOG_INFO("CameraRealTimeFrame: Saved BMP to: " + filename + " (" + std::to_string(rgbData.size()) + " bytes)");
    return true;
}

std::vector<uint8_t> CameraRealTimeFrame::getBmpData() const {
    std::vector<uint8_t> result;
    if (!isValid()) {
        return result;
    }

    std::vector<uint8_t> rgbData = frameData.toVector();
    if (rgbData.empty()) {
        return result;
    }

    BMPHeader header;
    header.width = frameData.width;
    header.height = frameData.height;
    header.imageSize = rgbData.size();
    header.fileSize = sizeof(BMPHeader) + rgbData.size();

    result.resize(sizeof(BMPHeader) + rgbData.size());
    std::memcpy(result.data(), &header, sizeof(BMPHeader));
    std::memcpy(result.data() + sizeof(BMPHeader), rgbData.data(), rgbData.size());

    return result;
}

int CameraRealTimeFrame::getWidth() const {
    return frameData.width;
}

int CameraRealTimeFrame::getHeight() const {
    return frameData.height;
}

int64_t CameraRealTimeFrame::getTimestamp() const {
    return frameData.timestamp_;
}
