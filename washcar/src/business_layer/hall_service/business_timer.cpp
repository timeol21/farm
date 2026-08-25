#include "business_layer/hall_service/business_timer.h"

#include <chrono>

BusinessTimer::BusinessTimer()
{

}

BusinessTimer::~BusinessTimer()
{

    stop();
}

bool BusinessTimer::start(std::function<void()> callback,int intervalMs)
{

    if(running_)
    {
        return true;
    }

    if(!callback)
    {
        return false;
    }

    callback_ = callback;

    if(intervalMs > 0)
    {
        intervalMs_ = intervalMs;
    }

    running_ = true;

    thread_ = std::thread(&BusinessTimer::run,this);

    return true;

}

void BusinessTimer::stop()
{

    running_ = false;

    if(thread_.joinable())
    {
        thread_.join();
    }

}

void BusinessTimer::run()
{

    while(running_)
    {

        if(callback_)
        {
            callback_();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs_)
        );

    }

}