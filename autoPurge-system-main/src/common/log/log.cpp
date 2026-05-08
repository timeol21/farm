#include "common/log/log_manager.h"
namespace fs = std::filesystem;

static std::string levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:   return "DEBUG";
        case LogLevel::INFO:    return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERROR:   return "ERROR";
        default:                return "UNKNOWN";
    }
}

static std::string getCurrentDate() {
    std::time_t t = std::time(nullptr);

    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif

    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return std::string(buf);
}
static std::string formatTime(std::time_t t)
{
    std::tm tm{};

#ifdef _WIN32
    // localtime_s(&tm, &t);   // Windows 线程安
#else
    localtime_r(&t, &tm);   // Linux 线程安全
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");

    return oss.str();
}

void cleanupOldLogs()//不用先实现，后续实现
{
    // 遍历目录
    // 找到 log_YYYY-MM-DD.txt
    // 解析日期
    // 删除超过7天的文件
}

LoggerManager& LoggerManager::instance(){
    static LoggerManager instance;
    return instance;
}

void LoggerManager::registerLogger(const std::string& name, std::shared_ptr<ILogger> logger){

    loggers_[name] = logger;
}

std::shared_ptr<ILogger> LoggerManager::getLogger(const std::string& name)
{
    
    auto it = loggers_.find(name);
    if (it != loggers_.end()) {
        return it->second;
    }
    return nullptr;
}


AsyncLogger::AsyncLogger(std::shared_ptr<ILogQueue> queue){
    m_queue = queue;
}

AsyncLogger::~AsyncLogger(){
    stop();
}

void AsyncLogger::log(LogLevel level, const std::string& message) {
    if(level < m_level) return ;

    LogMessage msg;
    msg.level = level;
    msg.message = message;
    msg.timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    msg.threadId = std::this_thread::get_id();
    m_queue->push(msg);

}

     
void AsyncLogger::addSink(std::shared_ptr<ILogSink> sink){
    m_sinks.push_back(sink);
}

     
bool AsyncLogger::start(){
    if(m_running){
        std::cerr << "log 已经在运行了\n";
        return false;
    }
    m_running = true;
    
    m_workerThread = std::thread(&AsyncLogger::worker,this);

    return true;
}
void AsyncLogger::worker() {
    while (m_running) {
        LogMessage msg;

        // 阻塞等待
        if (!m_queue->pop(msg)) {
            continue; // 队列被唤醒但没数据

        }

        // 分发给所有 sink
        for (auto& sink : m_sinks) {
            sink->write(msg);
        }
    }

    // ⭐ 退出前 flush 剩余日志（非常关键）
    LogMessage msg;
    while (m_queue->pop(msg)) {
        for (auto& sink : m_sinks) {
            sink->write(msg);
        }
    }
}

void AsyncLogger::stop(){
    if(!m_running) return ;
    
    m_running = false;
    if(m_queue){
        m_queue->notifyAll();
    }

    if(m_workerThread.joinable()){
        m_workerThread.join();
    }
}


void BlockingQueue::push(const LogMessage& msg) {
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_queue.push(msg);
    }
    m_cv.notify_one();
}

bool BlockingQueue::pop(LogMessage& msg) {
    std::unique_lock<std::mutex> lock(m_mutex);
    // 等待直到队列非空
    m_cv.wait(lock,[this]{
        return !m_queue.empty() || m_stop;
    });
    if(m_queue.empty()){
        return false;
    }
    msg = std::move(m_queue.front());
    m_queue.pop();
    return true;
}


void BlockingQueue::notifyAll() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_cv.notify_all(); // 唤醒所有线程退出
}


FileSink::FileSink(const std::string& filePath){
    m_basePath = filePath;

    m_currentDate = getCurrentDate();

    std::string fileName = buildFileName(m_currentDate);
    m_file.open(fileName, std::ios::app | std::ios::out);
    m_file.setf(std::ios::unitbuf);
    if(!m_file.is_open()){
        std::cerr << "打开日志失败: " << fileName << std::endl;
    }
}

void FileSink::write(const LogMessage& msg) {

    rotateIfNeeded(); //查看是否是今天

    if(!m_file.is_open()) return;
    std::ostringstream oss;
    oss << "[" << formatTime(msg.timestamp) << "]"
        << " [" << levelToString(msg.level) << "]"
        << " [T:" << std::hash<std::thread::id>{}(msg.threadId) << "]";
    if (!msg.module.empty()) {
        oss << " [" << msg.module << "]";
    }
    oss << " " << msg.message << "\n";
    m_file << oss.str();

    if(msg.level == LogLevel::ERROR) m_file.flush();
}

std::string FileSink::buildFileName(const std::string& date){
    return m_basePath + "_" +  date + ".txt";
}

void FileSink::rotateIfNeeded(){
    std::string today = getCurrentDate();

    if(today == m_currentDate){
        return ;
    }

    m_currentDate = today;

    if(m_file.is_open()){
        m_file.close();
    }

    std::string newFile = buildFileName(today);

    fs::create_directories(fs::path(newFile).parent_path());

    m_file.open(newFile,std::ios::app);

    if(!m_file.is_open()){
        std::cerr << "打开新的日志失败 " << newFile << std::endl;
    }
}


