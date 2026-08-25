#pragma once

#include <thread>
#include <atomic>
#include <functional>

class BusinessTimer
{

public:

    BusinessTimer();

    ~BusinessTimer();

public:

    bool start(std::function<void()> callback,int intervalMs);

    void stop();

private:

    void run();

    bool running() const;

    std::thread thread_;

    std::atomic<bool> running_ = false;

    std::function<void()> callback_;

    int intervalMs_ = 100;   // command_service从layer_buffer_queue中读取消息的频次


};