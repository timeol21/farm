#include "presentation_layer/http_response.h"
#include "business_layer/lobby/lobby_object.h"

int HttpResponse::mapToHttpStatus(ErrorCode::Code code)
{
    switch (code)
    {
        // ✅ 参数错误
        case ErrorCode::Code::WEB_PARAM_ERROR:
        case ErrorCode::Code::WEB_JSON_PARSE_ERROR:
        case ErrorCode::Code::WEB_INVALID_REQUEST:
            return 400;

        // ✅ 限流
        case ErrorCode::Code::WEB_TOO_MANY_REQUESTS:
            return 429;

        // ✅ 认证
        case ErrorCode::Code::AUTH_TOKEN_INVALID:
        case ErrorCode::Code::AUTH_TOKEN_MISSING:
            return 401;

        case ErrorCode::Code::AUTH_PERMISSION_DENIED:
            return 403;

        case ErrorCode::Code::AUTH_USER_NOT_EXIST:
            return 404;

        case ErrorCode::Code::AUTH_USER_LOCKED:
            return 423; // 🔥 很多人不知道这个

        // ✅ 资源状态
        case ErrorCode::Code::CAMERA_NOT_FOUND:
            return 404;

        case ErrorCode::Code::CAMERA_OFFLINE:
            return 503;

        // ✅ 服务错误
        case ErrorCode::Code::SERVER_DB_ERROR:
        case ErrorCode::Code::SERVER_THIRD_PARTY_ERROR:
            return 502;

        case ErrorCode::Code::SERVER_RESOURCE_EXHAUSTED:
            return 503;

        case ErrorCode::Code::SERVER_CONFIG_ERROR:
        case ErrorCode::Code::SERVER_INTERNAL_ERROR:
            return 500;

        default:
            return 500;
    }
}
static std::string getReasonPhrase(int statusCode)
{
    switch (statusCode) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 423: return "Locked";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        default:  return "Unknown";
    }
}


HttpResponse HttpResponse::Json(int statusCode, const std::string& json)
{
    HttpResponse resp;
    resp.statusCode = statusCode;
    resp.reasonPhrase = getReasonPhrase(statusCode);
    resp.contentType = "application/json; charset=utf-8";
    resp.body.assign(json.begin(), json.end());
    return resp;
}
HttpResponse HttpResponse::Text(int statusCode, const std::string& text)
{
    HttpResponse resp;
    resp.statusCode = statusCode;
    resp.reasonPhrase = getReasonPhrase(statusCode);
    resp.contentType = "text/plain; charset=utf-8";
    resp.body.assign(text.begin(), text.end());
    return resp;
}

HttpResponse HttpResponse::Binary(int statusCode,const std::string& contentType,const std::vector<unsigned char>& data)
{
    HttpResponse resp;
    resp.statusCode = statusCode;
    resp.reasonPhrase = getReasonPhrase(statusCode);
    resp.contentType = contentType;
    resp.body = data;
    return resp;
}


HttpResponse HttpResponse::fromError(ErrorCode::Code code, const std::string& message)
{
    nlohmann::json resp;
    resp["success"] = false;
    resp["errorCode"] = static_cast<int>(code);
    resp["message"] = message;

    return Json(mapToHttpStatus(code), resp);
}



// nlohmann::json HttpResponse::toJson(const SolenoidValveOperationResult& obj)
// {
//     return {
//         {"deviceId", obj.getDeviceId()},
//         {"plcId", obj.getPlc()},
//         {"code", obj.getCode()}
//     };
// }

// nlohmann::json HttpResponse::toJson(const SolenoidStatus& obj){
//     return{
//         {"status",obj.isOpen()}
//     };
// }

// nlohmann::json HttpResponse::toJson(const TempHumidSensorStatus& obj){
//     return{
//         {"tempHumidStatus",obj.getStatus()},
//         {"humidity",obj.getHumidity()},
//         {"tempature",obj.getTempature()},
//     } ; 
// }

// nlohmann::json HttpResponse::toJson(const InfraredSensorStatus& obj){
//     return{
//         {"status",obj.getStatus()}
//     };
// }

// nlohmann::json HttpResponse::toJson(const SmokeDetectorStatus& obj){
//     return{
//         {"status",obj.getStatus()}
//     };
// }

// nlohmann::json HttpResponse::toJson(const WaterLevelSensorStatus& obj){
//     return{
//         {"status",obj.getStatus()}
//     };
// }