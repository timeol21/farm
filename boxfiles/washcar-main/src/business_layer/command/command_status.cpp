#include "business_layer/command/command_status.h"
#include <string>
#include <iostream>
#include <unordered_map>

// ö��ֵת�ַ����ĺ���
std::string statusToString(Status status) {
    // ����ö��ֵ���ַ�����ӳ���
    static const std::unordered_map<Status, std::string> statusMap = {
        {Status::Success, "Success"},
        {Status::Failure, "Failure"},
        {Status::InProgress, "InProgress"},
        {Status::Timeout, "Timeout"},
        {Status::InvalidCommand, "InvalidCommand"},
        {Status::Unauthorized, "Unauthorized"},
        {Status::NotFound, "NotFound"},
        {Status::InternalError, "InternalError"}
    };

    // ����ӳ�䣬�Ҳ�������Ĭ��ֵ
    auto it = statusMap.find(status);
    if (it != statusMap.end()) {
        return it->second;
    }
    return "UnknownStatus";
}

// ��־��ʼ�����ߺ�����ȫ��/������
//void initCommandStatusLogger() {
//    try {
//        // ֻ��ʼ��һ��logger��spdlog���Զ������������ظ�����Ҳ�᷵���Ѵ��ڵ�logger��
//        auto logger = spdlog::basic_logger_mt("command_status_logger", "logs/command_status.log");
//        // ���û��壬����ˢ��
//        logger->flush_on(spdlog::level::info);
//    }
//    catch (const spdlog::spdlog_ex& ex) {
//        std::cerr << "Failed to initialize command status logger: " << ex.what() << std::endl;
//    }
//	std::cerr << "Command status logger initialized successfully." << std::endl;
//}

CommandStatus::CommandStatus(Command cmd, Status status, const std::string& message)
{
    _command = cmd;
    _status = status;
    _message = message;
	logStatusChange(); // ����ʱ��¼��ʼ״̬
}

void CommandStatus::setStatus(Status status)
{
	_status = status;
	logStatusChange(); // ÿ��״̬�ı�ʱ��¼��־
}

void CommandStatus::logStatusChange()
{
   // auto logger = spdlog::get("command_status_logger");
   // if (logger) { // ���logger�Ƿ��ʼ���ɹ�
   //     logger->info(
   //         "Command status updated | Command: {}, New Status: {}, Message: {}",
   //         _command.getCommand(),
   //         statusToString(_status),
   //         _message
   //     );
   //     std::cerr << "Logged command status change: Command: " << _command.getCommand()
   //               << ", Status: " << statusToString(_status)
			//<< ", Message: " << _message << std::endl;
   // }
   // else {
   //     std::cerr << "Logger not initialized. Command: " << _command.getCommand()
   //               << ", Status: " << statusToString(_status)
			//<< ", Message: " << _message << std::endl;
   // }
	LOG_INFO("Command status updated | Command: " + _command.getCommand() + 
        ", New Status: " + statusToString(_status) + ", Message: " + _message);

    std::cerr << "Logged command status change: Command: " << _command.getCommand()
              << ", Status: " << statusToString(_status)
		<< ", Message: " << _message << std::endl;
}

CommandQueue::CommandQueue()
{
	//initCommandStatusLogger(); // ��ʼ����־ϵͳ
}

CommandQueue::~CommandQueue()
{
    //auto logger = spdlog::get("command_status_logger");
    //if (logger) {
    //    logger->info("CommandQueue is being destroyed, flushing logs...");
    //    logger->flush(); // ǿ��ˢ�̣�������־��ʧ
    //    spdlog::drop("command_status_logger"); // ���ٸ�logger
    //}
}