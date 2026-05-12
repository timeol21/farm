#ifndef CAMERA_HISTORY_VIDEO_H
#define CAMERA_HISTORY_VIDEO_H

#include <string>

class CameraHistoryVideo {
    public:

        CameraHistoryVideo() = default;
        ~CameraHistoryVideo() = default;
        CameraHistoryVideo(const std::string& videoUrl);

    private:
        std::string videoUrl_;

};

#endif