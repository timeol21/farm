#include "job_scheduler.h"
#include "mqtt_service.h"
#include "mqtt_topics.h"
#include <unistd.h>
JobScheduler::JobScheduler(size_t workerCount,IDeviceManager* devMgr/*,ITaskResultPublisher* publisher*/)
    :pool_(workerCount),devMgr_(devMgr)/*,publisher_(publisher)*/
{
    dispatcher_ = std::thread(&JobScheduler::dispatchLoop,this);
}

JobScheduler::~JobScheduler()
{
    stop_ = true;
    cv_.notify_all();
    if(dispatcher_.joinable()) dispatcher_.join();
    if (downloadUpload_.joinable()) {
        downloadUpload_.join();
    }
}
// void JobScheduler::setPublisher(ITaskResultPublisher* publisher)
// {
//     publisher_ = publisher;
// }
int JobScheduler::submit(std::shared_ptr<ITask> task, const std::string& source)
{  
    auto tcb = std::make_shared<TaskControlBlock>();
    tcb->id = nextId_++;
    tcb->task = task;
    tcb->status = TaskStatus::READY;
    tcb->name = task->name();
    tcb->enqueueTime = std::chrono::steady_clock::now();
    tcb->source = source;
   
    {
        std::lock_guard<std::mutex> lk(mtx_); 
        readyQueue_.push(tcb);
        taskTable_[tcb->id] = tcb;
    }
    cv_.notify_one();
    return tcb->id;

}

TaskStatus JobScheduler::getTaskStatus(int taskId)
{
    std::lock_guard<std::mutex> lk(mtx_);
    if(taskTable_.count(taskId))
    {
        return taskTable_[taskId]->status;
    }
    return TaskStatus::FAILED;
}

void JobScheduler::dispatchLoop()
{
    while(!stop_)
    {
        std::shared_ptr<TaskControlBlock> tcb = nullptr;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait(lk,[this]{
                return stop_ || !readyQueue_.empty();
            });

            if(stop_) break;

            tcb = readyQueue_.front();
            readyQueue_.pop();

            tcb->status = TaskStatus::RUNNING;
            tcb->startTime = std::chrono::steady_clock::now();
            runningSet_.insert(tcb->id);
        }

        pool_.submit([this,tcb](){
            ITaskResultPublisher* pub = (tcb->source == "http") ? httpPublisher_ : mqttPublisher_;
            TaskContext ctx{.taskId = tcb->id,.devMgr = this->devMgr_,.publisher = pub,.source = tcb->source};
            try{
                tcb->task->run(ctx);
                tcb->status = TaskStatus::FINISHED;
            }catch(...){
                tcb->status = TaskStatus::FAILED;
            }
            {
                std::lock_guard<std::mutex> lk(this->mtx_);
                tcb->duration = std::chrono::steady_clock::now() - tcb->startTime;
                runningSet_.erase(tcb->id);
            }

        });
    }
}


void JobScheduler::downloadAndUploadVideo(DownloadVideoFile in) {
    {
        std::lock_guard<std::mutex> lock(uploadMutex);
        if (uploadFile.load()) {
            nlohmann::json j;
            j["erro"] = "已经存在下载视频，请过会重试";
            j["code"] = -1;
            httpPublisher_->publish(RESULT_VIDEO_HISTORY_FILE_TOPIC, j.dump(4));
            return;
        }
        fileUploader = std::make_shared<HttpFileUploader>();
    }

    DownloadReadyFile out;
    bool ok = devMgr_->downloadRecordFile(in, out);
    std::cout << "JobScheduler::downloadAndUploadVideo 获取数据" << std::endl;

    if (!ok) {
        std::lock_guard<std::mutex> lock(uploadMutex);
        std::cout << "JobScheduler::downloadAndUploadVideo 获取数据失败" << std::endl;
        fileUploader.reset();
        return;
    }

    uploadFile.store(true);

    // 用 std::thread 管理线程，并保存到成员变量
    downloadUpload_ = std::thread([this, out = std::move(out)]() mutable {
        this->downloadAndUploadLoop(std::move(out));
    });
    // 不再 detach，确保可以在析构时 join
}

// 改造 uploadLoop，确保异常安全
void JobScheduler::downloadAndUploadLoop(DownloadReadyFile out) {
    std::shared_ptr<HttpFileUploader> uploader;
    try {
        {
            std::lock_guard<std::mutex> lock(uploadMutex);
            if (!fileUploader) {
                std::cerr << "JobScheduler::downloadAndUploadLoop 错误：fileUploader为空" << std::endl;
                uploadFile.store(false);
                return;
            }
            uploader = fileUploader; // 拷贝 shared_ptr
        }

        HttpFileUploader::UploadResult result;
        std::string fullUrl = httpPublisher_->getUrl() + "/" + RESULT_VIDEO_HISTORY_FILE_TOPIC;

        bool ok = uploader->uploadFile(fullUrl, out.getLocalPath(), out.getFileName(),
                                       out.getCameraId(), out.getNvrId(),
                                       result,
                                       [](size_t sent, size_t total) {//这里的回调函数没有用的 ，容易发生段错误
                                           if (total > 0) {
                                               int percent = static_cast<int>(sent * 100 / total);
                                               static std::mutex coutMutex;
                                               std::lock_guard<std::mutex> lock(coutMutex);
                                               std::cout << "上传进度: " << percent << "%" << std::endl;
                                           }
                                       });

        uploadFile.store(false);

        if (!ok) {
            std::cerr << "上传失败: " << result.errorMsg << std::endl;
            return;
        }

        if (result.httpCode == 201) {
            std::cout << "上传成功" << std::endl;
        } else {
            std::cout << "HTTP 返回错误: " << result.httpCode << std::endl;
        }

    } catch (const std::exception &e) {
        std::cerr << "downloadAndUploadLoop 异常: " << e.what() << std::endl;
        uploadFile.store(false);
    } catch (...) {
        std::cerr << "downloadAndUploadLoop 未知异常" << std::endl;
        uploadFile.store(false);
    }
}