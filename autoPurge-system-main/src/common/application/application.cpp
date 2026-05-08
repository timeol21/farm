#include "common/application/application.h"
#include <thread>
#include <chrono>
#include <unistd.h>

bool Application::init(){
    m_context = std::make_unique<ApplicationContext>();
    if(!m_context->build()){
        return false;
    }

    std::cout << ">>> 应用初始化成功" << std::endl;
    return true;
}

bool Application::start(){
    m_context->start();
    g_exit_signal = true;
    std::cout << ">>> 应用启动成功" << std::endl;
    return true;
}

void Application::run(){
    std::cout << ">>> 服务运行中，等待退出信号..." << std::endl;
    while (g_exit_signal) {
            // 这里可以放你的业务逻辑
        sleep(1);
    }
    std::cout << std::endl;
    std::cout << ">>> 收到停止指令，准备退出..." << std::endl;
}

void Application::stop(){

    m_context->stop();

    m_context->destroy();

    std::cout << ">>> 应用停止完成" << std::endl;
}

void Application::stopProject(){
    
}