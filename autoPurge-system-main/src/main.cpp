#include "common/application/application.h"
#include <csignal>   // 信号处理头文件
#include <unistd.h>  // getpid()
#include <atomic>
std::atomic<bool> g_exit_signal {false};

void signalHandler(int signum) {
    g_exit_signal = false;  // 只干这一件事！
}

int main(int argc, char* argv[]){
    // ====================== 注册信号处理 ======================
    signal(SIGINT,  signalHandler);  // Ctrl+C
    signal(SIGTERM, signalHandler);  // kill <pid>

    Application app;

    if(!app.init()){
        return -1;
    }

    if(!app.start()){
        return -2;
    }

    // ====================== 所有服务启动完成 ======================
    std::cout << std::endl;
    std::cout << "=============================================" << std::endl;
    std::cout << ">>> 系统启动成功！服务已就绪 (进程 ID: " << getpid() << ")" << std::endl; 
    std::cout << ">>> 按 Ctrl+C 停止服务" << std::endl;
    std::cout << "=============================================" << std::endl;

    app.run();

    app.stop();

    std::cout << ">>> 服务已关闭，系统退出成功" << std::endl;
    std::cout << "=============================================" << std::endl;

    return 0;
}