#include "SegmentManager.h"
#include "FileUtils.h"
#include "logger.h"
#include <filesystem>
namespace fs = std::filesystem;
SegmentManager::SegmentManager(const std::string& dir, const std::string& format, int durationSec)
    : dir_(dir), format_(format), durationSec_(durationSec), segmentIndex_(0) {
    lastStart_ = std::chrono::steady_clock::now();
}

bool SegmentManager::needNewSegment() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastStart_).count();
    return elapsed >= durationSec_;
}


void SegmentManager::cleanExpiredFiles(int retainDays)
{
    using namespace std::chrono;

    if (!fs::exists(dir_) || !fs::is_directory(dir_)) {
        return;
    }

    auto now = system_clock::now();
    auto expireTime = now - hours(24 * retainDays);

    for (const auto& entry : fs::directory_iterator(dir_)) {
        if (!entry.is_regular_file()) continue;

        auto ftime = fs::last_write_time(entry);
        auto sctp = time_point_cast<system_clock::duration>(
                        ftime - fs::file_time_type::clock::now() + now
                    );

        if (sctp < expireTime) {
            try {
                fs::remove(entry);
                LOG_INFO("Deleted expired video: " + entry.path().string());
            } catch (const std::exception& e) {
                LOG_WARNING("Failed to delete: " + entry.path().string()
                            + " error=" + e.what());
            }
        }
    }
}



std::unique_ptr<VideoStorage> SegmentManager::createSegment(AVFormatContext* inputCtx) {
    std::string outputFile = FileUtils::generateFileName(dir_,format_);
    auto storage = std::make_unique<VideoStorage>(outputFile, format_);
    if (!storage->initStorage(inputCtx)) {
        LOG_ERROR("Failed to init new segment storage");
        return nullptr;
    }

    lastStart_ = std::chrono::steady_clock::now();
    segmentIndex_++;

    LOG_INFO("Started new segment #" + std::to_string(segmentIndex_) + ": " + outputFile);
    return storage;
}