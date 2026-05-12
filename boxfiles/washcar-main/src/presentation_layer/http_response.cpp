#include "presentation_layer/http_response.h"
#include "business_layer/lobby/lobby_object.h"
static int mapToHttpStatus(ErrorCode::Code code)
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

HttpResponse HttpResponse::createResponse(const LobbyResult<SolenoidValveOperationResult>& opResult){
    nlohmann::json resp;

    if (opResult.success) {
        resp["success"] = true;
        resp["data"] = {
            {"deviceId", opResult.data->getDeviceId()},
            {"plcId", opResult.data->getDeviceId()},
            {"code", opResult.data->getCode()},
            {"message", opResult.data->getMessage()}
        };
        
        return {200, resp.dump()};
    }

    
    resp["success"] = false;
    resp["errorCode"] = static_cast<int>(opResult.errorCode);
    resp["message"] = opResult.message;

    int status = mapToHttpStatus(opResult.errorCode);

   
    return {status, resp.dump()};
}