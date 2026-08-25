#ifndef JOB_SCHEDULER_H
#define JOB_SCHEDULER_H

#include "itask.h"
#include "thread_pool.h"
#include "idevice_manager.h"
#include <queue>
#include <mutex>
#include <thread>
#include <set>
#include <condition_variable>
#include <map>
#include "itask_result_publisher.h"
#include "http_file_uploader.h"
class JobScheduler{
public:
    JobScheduler(size_t workerCount,IDeviceManager* devMgr);
    ~JobScheduler();

    void setMqttPublisher(ITaskResultPublisher* p) { mqttPublisher_ = p; }
    void setHttpPublisher(ITaskResultPublisher* p) { httpPublisher_ = p; }

    ITaskResultPublisher* getPublisher(const std::string& source){
        if(source == "http") return httpPublisher_;
        return mqttPublisher_;
    }

    // submit task; source can be "mqtt" or "http" (default "mqtt")
    int submit(std::shared_ptr<ITask> task, const std::string& source = "mqtt");
    // void setPublisher(ITaskResultPublisher* publisher);
    TaskStatus getTaskStatus(int taskId);

    void downloadAndUploadVideo(DownloadVideoFile in);


private:
    void dispatchLoop();


    void downloadAndUploadLoop(DownloadReadyFile out);

private:
    ThreadPool pool_;
    std::thread dispatcher_;

    std::mutex mtx_;
    std::condition_variable cv_;

    std::queue<std::shared_ptr<TaskControlBlock>> readyQueue_;
    std::set<int> runningSet_;
    std::map<int, std::shared_ptr<TaskControlBlock>> taskTable_;

    std::atomic_bool stop_{false};
    std::atomic_int nextId_{1};
    IDeviceManager* devMgr_;
    ITaskResultPublisher* mqttPublisher_;
    ITaskResultPublisher* httpPublisher_;

    std::mutex uploadMutex;
    std::thread downloadUpload_;
    
    std::shared_ptr<HttpFileUploader> fileUploader;

    std::atomic_bool uploadFile{false};



};



#endif