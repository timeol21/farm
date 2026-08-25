// http_file_uploader.h
#pragma once
#include <string>
#include <functional>
#include <curl/curl.h>
#include <sys/stat.h>
#include <iostream>   // 用于日志输出
#include <sys/stat.h> // stat函数需要
#include <cstdio>     // fopen/fclose
#include <memory>
class HttpFileUploader {
public:
    struct UploadResult {
        int httpCode = -1;
        std::string response;
        std::string errorMsg;
    };

    using ProgressCallback = std::function<void(size_t sent, size_t total)>;

    HttpFileUploader(){}
    ~HttpFileUploader() {
        curl_global_cleanup();
        std::cerr << "[LOG] curl全局资源已清理" << std::endl;
    };

    bool uploadFile( const std::string& url,const std::string& localFilePath, const std::string& fileNameEx,const std::string& cameraId,const std::string& nvrId,UploadResult& result,ProgressCallback progressCb = nullptr,const std::string& contentType = "video/mp4");
    
    bool checkFile(const std::string& path, size_t& fileSize);
private:
    


    
};
