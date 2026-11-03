#include "data_layer/camera/camera_history_video.h"
#include "common/log/log_manager.h"

#include <sys/stat.h>
#include <dirent.h>
#include <algorithm>
#include <cstring>
#include <ctime>

extern "C" {
#include <libavutil/time.h>
}

CameraHistoryVideo::CameraHistoryVideo(const std::string& outputPath)
    : currentPath_(outputPath),
      outputContext_(nullptr),
      outputStream_(nullptr),
      startTime_(0),
      frameCount_(0) {
}

CameraHistoryVideo::~CameraHistoryVideo() {
    stopRecording();
}

bool CameraHistoryVideo::startRecording(const std::string& outputPath, AVFormatContext* inputFormatCtx) {
    if (isRecording_) {
        LOG_WARNING("CameraHistoryVideo: Already recording");
        return true;
    }

    if (!inputFormatCtx) {
        LOG_ERROR("CameraHistoryVideo: Input format context is null");
        return false;
    }

    currentPath_ = outputPath;
    
    if (!initOutputContext(inputFormatCtx, outputPath)) {
        LOG_ERROR("CameraHistoryVideo: Failed to initialize output context");
        return false;
    }

    int ret = avio_open(&outputContext_->pb, outputPath.c_str(), AVIO_FLAG_WRITE);
    if (ret < 0) {
        char err_buf[128];
        av_strerror(ret, err_buf, sizeof(err_buf));
        LOG_ERROR("CameraHistoryVideo: Failed to open output file: " + std::string(err_buf));
        avformat_free_context(outputContext_);
        outputContext_ = nullptr;
        return false;
    }

    ret = avformat_write_header(outputContext_, nullptr);
    if (ret < 0) {
        char err_buf[128];
        av_strerror(ret, err_buf, sizeof(err_buf));
        LOG_ERROR("CameraHistoryVideo: Failed to write file header: " + std::string(err_buf));
        avio_closep(&outputContext_->pb);
        avformat_free_context(outputContext_);
        outputContext_ = nullptr;
        return false;
    }

    startTime_ = av_gettime();
    frameCount_ = 0;
    isRecording_ = true;

    LOG_INFO("CameraHistoryVideo: Started recording to " + outputPath);
    return true;
}

bool CameraHistoryVideo::stopRecording() {
    if (!isRecording_) {
        return true;
    }

    isRecording_ = false;

    if (outputContext_) {
        av_write_trailer(outputContext_);
        avio_closep(&outputContext_->pb);
        avformat_free_context(outputContext_);
        outputContext_ = nullptr;
        outputStream_ = nullptr;
    }

    LOG_INFO("CameraHistoryVideo: Stopped recording. Total frames: " + std::to_string(frameCount_));
    return true;
}

bool CameraHistoryVideo::writePacket(AVPacket* packet) {
    if (!isRecording_ || !outputContext_ || !outputStream_ || !packet) {
        return false;
    }

    AVPacket* pkt = av_packet_clone(packet);
    if (!pkt) {
        LOG_ERROR("CameraHistoryVideo: Failed to clone packet");
        return false;
    }

    av_packet_rescale_ts(pkt, AV_TIME_BASE_Q, outputStream_->time_base);
    pkt->stream_index = outputStream_->index;

    int ret = av_interleaved_write_frame(outputContext_, pkt);
    av_packet_free(&pkt);

    if (ret < 0) {
        char err_buf[128];
        av_strerror(ret, err_buf, sizeof(err_buf));
        // 写入失败可能是持续的，每帧打会刷屏。30 帧抑制一次。
        static int writeFailCounter = 0;
        if (++writeFailCounter == 1 || writeFailCounter % 30 == 0) {
            LOG_WARNING("CameraHistoryVideo: Failed to write packet: " + std::string(err_buf)
                        + ", count=" + std::to_string(writeFailCounter));
        }
        return false;
    }

    frameCount_++;
    return true;
}

std::vector<std::string> CameraHistoryVideo::getSegments() {
    std::vector<std::string> segments;

    if (currentPath_.empty()) {
        return segments;
    }

    size_t lastSlash = currentPath_.find_last_of("/\\");
    std::string dir = (lastSlash != std::string::npos) ? currentPath_.substr(0, lastSlash) : ".";
    std::string basename = (lastSlash != std::string::npos) ? currentPath_.substr(lastSlash + 1) : currentPath_;

    size_t dotPos = basename.rfind('.');
    std::string prefix = (dotPos != std::string::npos) ? basename.substr(0, dotPos) : basename;
    std::string extension = (dotPos != std::string::npos) ? basename.substr(dotPos) : "";

    DIR* dirHandle = opendir(dir.c_str());
    if (!dirHandle) {
        return segments;
    }

    struct dirent* entry;
    while ((entry = readdir(dirHandle)) != nullptr) {
        std::string filename(entry->d_name);
        if (filename.find(prefix) == 0 && filename != basename) {
            std::string fullPath = dir + "/" + filename;
            segments.push_back(fullPath);
        }
    }

    closedir(dirHandle);
    std::sort(segments.begin(), segments.end());
    return segments;
}

void CameraHistoryVideo::cleanExpiredSegments(int keepDays) {
    if (currentPath_.empty() || keepDays <= 0) {
        return;
    }

    size_t lastSlash = currentPath_.find_last_of("/\\");
    std::string dir = (lastSlash != std::string::npos) ? currentPath_.substr(0, lastSlash) : ".";

    time_t now = time(nullptr);
    time_t cutoff = now - (keepDays * 24 * 60 * 60);

    DIR* dirHandle = opendir(dir.c_str());
    if (!dirHandle) {
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dirHandle)) != nullptr) {
        std::string filename(entry->d_name);
        if (filename == "." || filename == "..") {
            continue;
        }

        std::string fullPath = dir + "/" + filename;
        struct stat st;
        if (stat(fullPath.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            if (st.st_mtime < cutoff) {
                if (remove(fullPath.c_str()) == 0) {
                    LOG_INFO("CameraHistoryVideo: Removed expired segment: " + fullPath);
                }
            }
        }
    }

    closedir(dirHandle);
}

bool CameraHistoryVideo::initOutputContext(AVFormatContext* inputFormatCtx, const std::string& outputPath) {
    AVOutputFormat* outputFormat = av_guess_format("mp4", outputPath.c_str(), nullptr);
    if (!outputFormat) {
        LOG_ERROR("CameraHistoryVideo: Failed to guess output format");
        return false;
    }

    int ret = avformat_alloc_output_context2(&outputContext_, outputFormat, nullptr, outputPath.c_str());
    if (!outputContext_) {
        LOG_ERROR("CameraHistoryVideo: Failed to allocate output context");
        return false;
    }

    int videoStreamIndex = av_find_best_stream(inputFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoStreamIndex < 0) {
        LOG_ERROR("CameraHistoryVideo: Failed to find video stream");
        avformat_free_context(outputContext_);
        outputContext_ = nullptr;
        return false;
    }

    AVStream* inputStream = inputFormatCtx->streams[videoStreamIndex];
    
    outputStream_ = avformat_new_stream(outputContext_, nullptr);
    if (!outputStream_) {
        LOG_ERROR("CameraHistoryVideo: Failed to create new output stream");
        avformat_free_context(outputContext_);
        outputContext_ = nullptr;
        return false;
    }

    ret = avcodec_parameters_copy(outputStream_->codecpar, inputStream->codecpar);
    if (ret < 0) {
        LOG_ERROR("CameraHistoryVideo: Failed to copy codec parameters");
        avformat_free_context(outputContext_);
        outputContext_ = nullptr;
        return false;
    }

    outputStream_->time_base = inputStream->time_base;
    outputStream_->codecpar->codec_tag = 0;

    return true;
}
