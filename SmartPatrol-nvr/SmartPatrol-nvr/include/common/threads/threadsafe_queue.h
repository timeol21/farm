#ifndef THREADSAFE_QUEUE_H
#define THREADSAFE_QUEUE_H
#include <queue>
#include <memory>
#include <mutex>
#include <condition_variable>
template<typename T>
class ThreadSafeQueue
{
public:
	ThreadSafeQueue() 
	{}
	ThreadSafeQueue(ThreadSafeQueue const& other) 
	{
		std::lock_guard<std::mutex> lk(other.mut);
		data_queue = other.data_queue;
	}
	ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;
	void push(T new_value) 
	{
		std::lock_guard<std::mutex> lk(mut);
		data_queue.push(new_value);
		data_cond.notify_one();
	}

	bool try_push(T new_value)
	{
		std::lock_guard<std::mutex> lk(mut);
		if(data_queue.size() >= max_size_){
			return false;
		}
		data_queue.push(new_value);
		data_cond.notify_one();
		return true;
	}




	bool try_pop(T& value) 
	{
		std::lock_guard<std::mutex> lk(mut);
		if (data_queue.empty()) return false;
		value = data_queue.front();
		data_queue.pop();
		return true;
	}
	std::shared_ptr<T> try_pop() 
	{
		std::lock_guard<std::mutex> lk(mut);
		if (data_queue.empty()) return std::shared_ptr<T>();
		std::shared_ptr<T> res(std::make_shared<T>(data_queue.front()));
		data_queue.pop();
		return res;
	}
	void wait_and_pop(T& value)
	{
		std::unique_lock<std::mutex> lk(mut);
		data_cond.wait(lk, [this] {return !data_queue.empty(); });
		value = data_queue.front();
		data_queue.pop();
	}
	std::shared_ptr<T> wait_and_pop()
	{
		std::unique_lock<std::mutex> lk(mut);
		data_cond.wait(lk, [this] {return !data_queue.empty(); });
		std::shared_ptr<T> res(std::make_shared<T>(data_queue.front()));
		data_queue.pop();
		return res;
	}
	bool empty() const
	{
		std::lock_guard<std::mutex> lk(mut);
		return data_queue.empty();
	}


private:
	mutable std::mutex mut;
	size_t max_size_ = 32;
	std::queue<T> data_queue;
	std::condition_variable data_cond;

};

#endif