#ifndef TASK_RESULT_PUBLISHER_H
#define TASK_RESULT_PUBLISHER_H

#include "itask_result_publisher.h"
#include "http_client.h"
#include "mqtt_service.h"
#include <curl/curl.h>
class MqttPublisher : public ITaskResultPublisher
{
public:
    MqttPublisher(MqttService* mqtt) : mqtt_(mqtt) {mqtt_->start();}
    void publish(const std::string& topic,
                 const std::string& message) override {
        mqtt_->publish(topic, message);
    }
    std::string getUrl() override {}
private:
    MqttService* mqtt_;
};
// 构造时指定你的 HTTP 服务器地址，例如：http://127.0.0.1:8080/report
class HttpPublisher : public ITaskResultPublisher {
public:
    explicit HttpPublisher(const std::string& url)
        : url_(url) 
    {
        curl_global_init(CURL_GLOBAL_ALL);
    }

    ~HttpPublisher() {
        curl_global_cleanup();
    }

    void publish(const std::string& topic,
                 const std::string& message) override 
    {
        CURL* curl = curl_easy_init();
        if (!curl) return;
        std::string full_url = url_ + "/" + topic;

        printf("【调试】准备发送的JSON数据大小：%lu 字节\n", message.size());
    
        // ========== 优化2：设置curl参数（增加超时/错误日志） ==========
        // 完整URL（已包含路径，无需再拼接）
        curl_easy_setopt(curl, CURLOPT_URL, full_url.c_str());
        // POST请求
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        // POST数据（标准JSON）
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, message.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, message.size());
        // 设置JSON请求头
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        // 超时时间（避免卡住）
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        // 开启错误日志（便于调试）
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

        // 执行POST请求
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            // 打印错误信息（调试用）
            fprintf(stderr, "回调失败：%s\n", curl_easy_strerror(res));
        } else {
            printf("回调成功！响应码：%d\n", res);
        }

        // 清理资源
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }

    std::string getUrl()  override {return url_;}

private:
    std::string url_;
};
// class HttpPublisher : public ITaskResultPublisher {
// public:
//     void publish(const std::string& topic,
//                  const std::string& message) override {
//        std::lock_guard<std::mutex> lk(mu_);
//        lastTopic_ = topic;
//        lastMessage_ = message;
//        ready_ = true;
//        cv_.notify_one();
//     }

//     // wait for a published message (returns empty string if timeout)
//     std::string waitForMessage(int timeoutMs = 1000) {
//         std::unique_lock<std::mutex> lk(mu_);
//         if (!ready_) {
//             cv_.wait_for(lk, std::chrono::milliseconds(timeoutMs));
//         }
//         ready_ = false;
//         return lastMessage_;
//     }

//     std::string lastTopic() const { return lastTopic_; }

// private:
//     mutable std::mutex mu_;
//     std::condition_variable cv_;
//     bool ready_ = false;
//     std::string lastTopic_;
//     std::string lastMessage_;
// };

#endif