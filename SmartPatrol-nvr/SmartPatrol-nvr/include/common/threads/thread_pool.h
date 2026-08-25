#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <iostream>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <chrono>
#include <functional> 
#include "threadsafe_queue.h"
#include "join_threads.h"
#include <atomic>
class ThreadPool
{
private:
    std::atomic_bool done;
    ThreadSafeQueue<std::function<void()>> work_queue;
    std::vector<std::thread> threads;
    join_threads joiner;
    std::condition_variable wake_cv;
    std::mutex wake_mtx;

    void worker_thread()
    {
        while (true)
        {
            std::function<void()> task;

            // 先尝试获取任务
            if (work_queue.try_pop(task))
            {
                task();
                continue;
            }

            // 如果已经 done，则退出
            if (done)
                return;

            // 否则阻塞等待
            std::unique_lock<std::mutex> lock(wake_mtx);
            wake_cv.wait_for(lock, std::chrono::milliseconds(50));
        }
    }

public:
    ThreadPool(size_t thread_count = 8)
        : done(false), joiner(threads)
    {
        for (size_t i = 0; i < thread_count; ++i)
        {
            threads.push_back(std::thread(&ThreadPool::worker_thread, this));
        }
    }

    ~ThreadPool()
    {
        done = true;
        wake_cv.notify_all();   // <-- 唤醒所有等待线程
    }

    template<typename F>
    void submit(F f)
    {
        work_queue.push(std::function<void()>(f));
        wake_cv.notify_one();    // <-- 通知 worker 有任务
    }
};

#endif