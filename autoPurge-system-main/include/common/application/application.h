#pragma once
#include "common/log/log_manager.h"

#include "common/application/application_context.h"


extern std::atomic<bool> g_exit_signal;
class Application{
public:

    Application() = default;
    ~Application() = default;
    
    static Application& getInstance() {
        static Application instance;
        return instance;
    }

    bool init();

    bool start();

    void run();

    void stop();
    
    void stopProject();

private:

    std::unique_ptr<ApplicationContext> m_context;

    // LogManager m_log_manager;

    bool running_ = false;
};