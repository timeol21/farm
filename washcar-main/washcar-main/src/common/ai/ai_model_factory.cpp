#include "common/ai/ai_model_factory.h"

#include "common/log/log_manager.h"

#include <stdexcept>

namespace washcar {
namespace ai {

AiModelFactory& AiModelFactory::instance() {
    static AiModelFactory factory;
    return factory;
}

std::shared_ptr<IAiModel> AiModelFactory::acquire(const cfg::AiModelConfig& cfg) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_models.find(cfg.modelId);
    if (it != m_models.end()) {
        if (auto alive = it->second.lock()) {
            return alive;   // 还有区域在用, 直接复用
        }
        m_models.erase(it); // 上一个使用者已全部释放, 重新加载
    }

    auto creator = m_creator ? m_creator : &createAiModel;
    std::unique_ptr<IAiModel> created = creator(cfg.modelKind);
    if (!created) {
        throw std::runtime_error("[AiModelFactory] unknown modelKind: " + cfg.modelKind
                                 + ", modelId=" + cfg.modelId);
    }
    if (!created->load(cfg)) {
        throw std::runtime_error("[AiModelFactory] load model failed, modelId=" + cfg.modelId);
    }

    std::shared_ptr<IAiModel> model(std::move(created));
    m_models[cfg.modelId] = model;
    LOG_INFO("[AiModelFactory] loaded modelId=" + cfg.modelId + ", kind=" + cfg.modelKind);
    return model;
}

std::size_t AiModelFactory::loadedCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::size_t alive = 0;
    for (const auto& entry : m_models) {
        if (!entry.second.expired()) ++alive;
    }
    return alive;
}

void AiModelFactory::releaseExpired() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_models.begin(); it != m_models.end();) {
        it = it->second.expired() ? m_models.erase(it) : std::next(it);
    }
}

void AiModelFactory::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_models.clear();
}

void AiModelFactory::setCreatorForTest(
    std::function<std::unique_ptr<IAiModel>(const std::string&)> creator) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_creator = std::move(creator);
}

}  // namespace ai
}  // namespace washcar
