#ifndef JOIN_THREADS_H
#define JOIN_THREADS_H

#include <vector>
#include <thread>

class join_threads
{
public:
    explicit join_threads(std::vector<std::thread>& threads_)
        :threads(threads_)
    {}
    ~join_threads()
    {
        for(unsigned long i = 0; i < threads.size(); ++i)
        {
            if(threads[i].joinable()) threads[i].join();
        }
    }



private:
    std::vector<std::thread>& threads;    
};

#endif