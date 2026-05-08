#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>

namespace BoxSystem {

class ThreadPool {
private:
    std::vector<std::thread> workers;        // 线程池中的工人们
    std::queue<std::function<void()>> tasks; // 任务队列
    
    std::mutex queueMutex;                  // 保护队列的锁
    std::condition_variable condition;      // 唤醒工人的铃声
    bool stop;                              // 停止标志

public:
    /**
     * @brief 构造函数
     * @param threadsCount 线程池开启的固定线程数量
     */
    explicit ThreadPool(size_t threadsCount = 4) : stop(false) {
        for(size_t i = 0; i < threadsCount; ++i) {
            workers.emplace_back([this] {
                while(true) {
                    std::function<void()> task;
                    {
                        // 准备领任务
                        std::unique_lock<std::mutex> lock(this->queueMutex);
                        this->condition.wait(lock, [this] { 
                            return this->stop || !this->tasks.empty(); 
                        });
                        
                        if(this->stop && this->tasks.empty()) return;
                        
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task(); // 执行具体任务（如：某传感器的 execute）
                }
            });
        }
    }

    /**
     * @brief 提交任务到线程池
     * 使用模板支持任何可调用对象
     */
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type> {
        
        using return_type = typename std::result_of<F(Args...)>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            if(stop) throw std::runtime_error("enqueue on stopped ThreadPool");
            tasks.emplace([task](){ (*task)(); });
        }
        condition.notify_one();
        return res;
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            stop = true;
        }
        condition.notify_all();
        for(std::thread &worker: workers) worker.join();
    }
};

} // namespace BoxSystem

#endif