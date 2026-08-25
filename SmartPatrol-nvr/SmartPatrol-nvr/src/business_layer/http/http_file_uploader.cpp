#include "http_file_uploader.h"
#include "logger.h"
#include <cstring>
static std::string getFileName(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos)
        return path;
    return path.substr(pos + 1);
}

static size_t curl_slist_count_custom(struct curl_slist *list) {
    size_t count = 0;
    while (list) {
        count++;
        list = list->next;
    }
    return count;
}

bool HttpFileUploader::uploadFile(
    const std::string& url,
    const std::string& localFilePath,
    const std::string& fileNameEx,
    const std::string& cameraId,
    const std::string& nvrId,
    UploadResult& result,
    ProgressCallback progressCb,
    const std::string& contentType
) {
    LOG_INFO("[HttpFileUploader::uploadFile] 开始执行文件上传");
   

    // 1. 处理文件名（强制MP4后缀）
    std::string fileName = getFileName(fileNameEx);
    if (fileName.find('.') == std::string::npos) {
        fileName += ".mp4";
        LOG_INFO("[HttpFileUploader::uploadFile] 文件名无后缀，强制添加.mp4，处理后文件名：" + fileName);
    }
    LOG_INFO("[HttpFileUploader::uploadFile] 最终上传文件名：" + fileName);

    // 2. 处理上传URL（保证以/结尾）
    std::string realUrl = url;
    if (!realUrl.empty() && realUrl.back() != '/') {
        realUrl += '/';
        LOG_INFO("[HttpFileUploader::uploadFile] URL未以/结尾，补充后：" + realUrl);
    }
    realUrl += fileName;
    LOG_INFO("[HttpFileUploader::uploadFile] 最终上传URL：" + realUrl);

    // 3. 检查文件有效性
    size_t fileSize = 0;
    LOG_INFO("[HttpFileUploader::uploadFile] 检查本地文件有效性，路径：" + localFilePath);
    if (!checkFile(localFilePath, fileSize)) {
        result.errorMsg = "file not found or invalid";
        LOG_ERROR("[HttpFileUploader::uploadFile] 文件检查失败，" + result.errorMsg + "，路径：" + localFilePath);
        return false;
    }
    LOG_INFO("[HttpFileUploader::uploadFile] 文件检查通过，路径：" + localFilePath + "，文件大小：" + std::to_string(fileSize) + "字节");

    // 4. 打开本地文件
    LOG_INFO("[HttpFileUploader::uploadFile] 打开本地文件，路径：" + localFilePath);
    FILE* fp = fopen(localFilePath.c_str(), "rb");
    if (!fp) {
        result.errorMsg = "open file failed, errno=" + std::to_string(errno) + " (" + strerror(errno) + ")";
        LOG_ERROR("[HttpFileUploader::uploadFile] " + result.errorMsg + "，路径：" + localFilePath);
        return false;
    }
    LOG_INFO("[HttpFileUploader::uploadFile] 本地文件打开成功，路径：" + localFilePath);

    // 5. 初始化CURL
    LOG_INFO("[HttpFileUploader::uploadFile] 初始化CURL句柄");
    CURL* curl = curl_easy_init();
    if (!curl) {
        result.errorMsg = "curl init failed";
        LOG_ERROR("[HttpFileUploader::uploadFile] " + result.errorMsg);
        fclose(fp);
        return false;
    }
    LOG_INFO("[HttpFileUploader::uploadFile] CURL句柄初始化成功");

    // 6. 构建HTTP请求头
    struct curl_slist* headers = nullptr;
    std::string contentTypeHeader = "Content-Type: " + contentType;
    std::string cameraIdHeader = "X-Camera-Id: " + cameraId;
    std::string nvrIdHeader = "X-NVR-Id: " + nvrId;
    
    LOG_INFO("[HttpFileUploader::uploadFile] 构建HTTP请求头：" + contentTypeHeader + " | " + cameraIdHeader + " | " + nvrIdHeader);
    
    // 添加自定义Header
    headers = curl_slist_append(headers, contentTypeHeader.c_str());
    headers = curl_slist_append(headers, cameraIdHeader.c_str());
    headers = curl_slist_append(headers, nvrIdHeader.c_str());
    // 禁用Expect: 100-continue，避免超时

     size_t headerCount = curl_slist_count_custom(headers);
    LOG_INFO("[HttpFileUploader::uploadFile] HTTP请求头添加完成，共" + std::to_string(headerCount) + "个Header");

    // 7. 设置CURL选项
    LOG_INFO("[HttpFileUploader::uploadFile] 配置CURL上传参数");
    // 设置请求头
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    // 设置上传URL
    curl_easy_setopt(curl, CURLOPT_URL, realUrl.c_str());
    // 启用上传模式
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    // 设置文件指针（读取数据来源）
    curl_easy_setopt(curl, CURLOPT_READDATA, fp);
    // 设置文件大小
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)fileSize);
    
    // 网络优化选项
    curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 0L);          // 关闭Nagle算法（大文件上传更高效）
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 60L);      // 连接超时60秒
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);        // 启用TCP保活
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 60L);        // 60秒无数据发保活包
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 10L);       // 每10秒发一次保活包
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 7200L);           // 总超时2小时（匹配Nginx）
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);             // 避免信号处理冲突
    curl_easy_setopt(curl, CURLOPT_MAX_SEND_SPEED_LARGE, (curl_off_t)1024*1024*5); // 限速5MB/s
    
    // SSL选项（禁用证书校验，生产环境建议开启）
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    // HTTP版本
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);

    LOG_INFO("[HttpFileUploader::uploadFile] 开始执行CURL上传请求，URL：" + realUrl + "，文件大小：" + std::to_string(fileSize) + "字节");
    // 8. 执行上传请求
    CURLcode ret = curl_easy_perform(curl);
    if (ret != CURLE_OK) {
        result.errorMsg = "CURL upload failed: " + std::string(curl_easy_strerror(ret));
        LOG_ERROR("[HttpFileUploader::uploadFile] " + result.errorMsg + "，CURL错误码：" + std::to_string(ret));
        
        // 清理资源
        if (headers) curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        fclose(fp);
        return false;
    }
    LOG_INFO("[HttpFileUploader::uploadFile] CURL请求执行成功，无CURL错误");

    // 9. 检查HTTP响应码
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.httpCode);
    LOG_INFO("[HttpFileUploader::uploadFile] 获取HTTP响应码：" + std::to_string(result.httpCode));
    
    if (result.httpCode != 200 && result.httpCode != 201 && result.httpCode != 204) {
        result.errorMsg = "HTTP error: " + std::to_string(result.httpCode);
        LOG_ERROR("[HttpFileUploader::uploadFile] " + result.errorMsg + "，期望响应码：200/201/204");
        
        // 清理资源
        if (headers) curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        fclose(fp);
        return false;
    }

    // 10. 清理资源
    LOG_INFO("[HttpFileUploader::uploadFile] 上传成功，开始清理资源");
    if (headers) {
        curl_slist_free_all(headers);
        headers = nullptr;
    }
    curl_easy_cleanup(curl);
    fclose(fp);

    LOG_INFO("[HttpFileUploader::uploadFile] 文件上传完成，HTTP响应码：" + std::to_string(result.httpCode) + 
             "，摄像头ID：" + cameraId + "，文件路径：" + localFilePath);
    return true;
}

// ========== 辅助函数：检查文件有效性 ==========
bool HttpFileUploader::checkFile(const std::string& path, size_t& fileSize) {
    LOG_INFO("[HttpFileUploader::checkFile] 检查文件有效性，路径：" + path);
    fileSize = 0;
    struct stat st;

    // 1. 获取文件状态
    if (stat(path.c_str(), &st) != 0) {
        LOG_ERROR("[HttpFileUploader::checkFile] 获取文件状态失败，路径：" + path + 
                  "，errno：" + std::to_string(errno) + " (" + strerror(errno) + ")");
        return false;
    }

    // 2. 检查是否为普通文件
    if (!S_ISREG(st.st_mode)) {
        LOG_ERROR("[HttpFileUploader::checkFile] 不是普通文件（可能是目录/设备文件），路径：" + path);
        return false;
    }

    // 3. 检查文件大小
    if (st.st_size <= 0) {
        LOG_ERROR("[HttpFileUploader::checkFile] 文件大小为0，路径：" + path);
        return false;
    }

    // 4. 设置文件大小
    fileSize = static_cast<size_t>(st.st_size);
    LOG_INFO("[HttpFileUploader::checkFile] 文件检查通过，路径：" + path + "，文件大小：" + std::to_string(fileSize) + "字节");
    return true;
}