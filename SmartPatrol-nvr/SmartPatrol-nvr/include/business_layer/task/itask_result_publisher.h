#ifndef I_TASK_RESULT_PUBLISHER_H
#define I_TASK_RESULT_PUBLISHER_H
#include <string>

class ITaskResultPublisher
{
public:
    virtual ~ITaskResultPublisher() = default;
    virtual void publish(const std::string& topic,const std::string& message) = 0;

    virtual std::string getUrl() = 0;
};

#endif