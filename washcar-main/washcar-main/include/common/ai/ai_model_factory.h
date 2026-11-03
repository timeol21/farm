#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "common/ai/ai_model.h"

namespace washcar {
namespace ai {

// AI 模型的统一加载工厂。
//   同一个 modelId 只加载一次: 第一个使用者创建并 load, 后续使用者拿同一个 shared_ptr;
//   全部使用者释放后 weak_ptr 自然过期, 下次 acquire 才重新加载。
// 一个模型可以被多个检测区域共用(指针共享), 省内存也省重复加载。
//
// 线程约定: IAiModel::run() 不是线程安全的(NPU 也只能串行), 调用方必须串行调 run();
//           检测服务目前只有一个检测任务线程, 天然满足。
class AiModelFactory {
public:
    static AiModelFactory& instance();

    // 取模型使用权。cfg.modelId 为键; 加载失败抛 std::runtime_error(与改造前 DetectionService 一致)
    std::shared_ptr<IAiModel> acquire(const cfg::AiModelConfig& cfg);

    // 当前仍被使用者持有的模型数(weak_ptr 未过期)
    std::size_t loadedCount() const;

    // 卸载所有已无使用者的条目(仅清理表; 有使用者的不动)
    void releaseExpired();

    // 停机/测试用: 清空表(有使用者时它们仍持有自己的 shared_ptr)
    void clear();

    // 测试注入: 替换"按 modelKind 造模型"的默认实现, 避免测试去碰 rknn
    void setCreatorForTest(std::function<std::unique_ptr<IAiModel>(const std::string&)> creator);

private:
    AiModelFactory() = default;

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, std::weak_ptr<IAiModel>> m_models;
    std::function<std::unique_ptr<IAiModel>(const std::string&)> m_creator;
};

}  // namespace ai
}  // namespace washcar
