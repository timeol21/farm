#pragma once
#include "common/erro_code.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;
template <typename T>
class LobbyResult{
public:
    bool success; // 操作是否成功
    ErrorCode::Code errorCode; // 错误码，成功时为 ErrorCode::Code::SUCCESS
    std::string message; // 相关信息的字符串
    std::optional<T> data; // 可选的数据字段，成功时包含返回数据，失败时为 std::nullopt

public:  
    
    static LobbyResult<T> Ok(const T& data){// 成功结果，包含数据
        return {true,ErrorCode::Code::SUCCESS,"success",data};
    } 

    static LobbyResult<T>Error(ErrorCode::Code code){//错误结果，包含错误码和对应的错误信息
        return {false,code,ErrorCode::getMessage(code),std::nullopt};
    } 
};

template <>
class LobbyResult<void>{
public:
    bool success; // 操作是否成功
    ErrorCode::Code errorCode; // 错误码，成功时为 ErrorCode::Code::SUCCESS
    std::string message; // 相关信息的字符串
    

public:  
    
    static LobbyResult<void> Ok(){// 成功结果，包含数据
        return {true,ErrorCode::Code::SUCCESS,"success"};
    } 

    static LobbyResult<void>Error(ErrorCode::Code code){//错误结果，包含错误码和对应的错误信息
        return {false,code,ErrorCode::getMessage(code)};
    } 
};