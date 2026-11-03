#ifndef BOX_CONFIG_RESULT_H
#define BOX_CONFIG_RESULT_H

#include <string>

class BoxConfigResult {
    public:

        BoxConfigResult() = default;
        ~BoxConfigResult() = default;

        BoxConfigResult(int code, const std::string& message);
    
    private:
        int code;
        std::string message;
};

#endif