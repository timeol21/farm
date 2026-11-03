#pragma once
#include "business_layer/detection/detection_object.h"
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// 图像数据视图:引用帧缓冲,不拷像素。
// 需求里"图像数据"是检测结果的必备字段,必须一直带着;
// 但早先这里是 std::vector<uint8_t> + 每轮 getBmpData() 深拷贝,
// 写结果缓冲时再拷一份 —— 检测每 2~6 秒一轮,一轮两份 6MB,
// 4GB 的盒子白白吃内存。帧缓冲本来就是 shared_ptr 持有的,
// 这里只多存一个引用,像素不重复占内存。
struct ImageDataView {
    std::shared_ptr<const uint8_t> data;   // RGB888 像素
    int width = 0;
    int height = 0;

    bool valid() const { return data != nullptr && width > 0 && height > 0; }
    std::size_t size() const {
        return valid() ? static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3 : 0;
    }
};

// 检测结果封装(需求字段: 序号、采集时间、摄像头编号、图像数据、调用模型编号、调用时间、识别时间、识别结果)
struct DetectionRecord {
    int64_t seq = 0;                  // 序号(检测服务自增)
    int64_t captureTime = 0;          // 采集时间(帧的采集时间)
    std::string cameraId;             // 摄像头编号
    ImageDataView imageData;          // 图像数据(RGB 字节,共享帧缓冲不拷贝)
    std::string modelId;              // 调用模型编号(本轮全部模型, 逗号拼接)
    int64_t callTime = 0;             // 调用时间(派发时刻)
    int64_t inferTime = 0;            // 识别时间(完成时刻)
    DetectionResult result;           // 识别结果(items + alarms)
};

// 检测结果缓冲: 按区域区分, 同源新结果覆盖旧结果
class DetectionResultCache {
public:
    void write(const std::string& regionId, const DetectionRecord& record);  // 检测结果写入(检测服务调用)
    DetectionRecord read(const std::string& regionId) const;                 // 读取指定区域检测结果(大厅/测试调用)
    void consumeAlarms(const std::string& regionId);                        // 取走待报项(上报后清空, 防重复报)
    void clear();

private:
    std::unordered_map<std::string, DetectionRecord> records_;   // regionId -> 最新结果
    mutable std::mutex mutex_;
};
