#pragma once
#include <string>
class ISafetyService {
public:
    
    virtual ~ISafetyService() = default;

    virtual bool authenticate() = 0;

    virtual bool authorize() = 0;
    /**
     * @brief AES-CBC 加密，输出 Base64
     * @param plaintext  明文（mqtt密码&过期时间&授权码）
     * @param key        密钥（FastBee 产品秘钥，16字节=128位 或 32字节=256位）
     * @return           Base64 密文，失败返回空字符串
     *
     * 加密参数（固定）：
     *   模式: CBC
     *   填充: PKCS5Padding
     *   偏移量(IV): "wumei-smart-open" (16字节)
     *   输出: Base64
     */
    virtual std::string aes_cbc_encrypt(const std::string& plaintext, const std::string& key) = 0;
    /**
     * @brief 生成过期时间戳（当前毫秒 + 24小时），用于密码加密
     * @return 毫秒时间戳字符串
     */
    virtual std::string generate_expire_time() = 0;
        /**
    * @brief 按规范加密 MQTT 密码
    * @param mqttPassword  MQTT 原始密码
    * @param productKey    产品密钥（用作 AES 密钥）
    * @param authCode      授权码（可选，为空则不拼接）
    * @return              Base64 密文
    *
    * 明文格式: mqttPassword & expireTime & authCode(可选)
    */
    virtual std::string encryptMqttPassword(const std::string& mqttPassword,
                                            const std::string& productKey,
                                            const std::string& authCode = "") = 0;

};  


class SafetyService : public ISafetyService {


public:
    SafetyService() = default;
    ~SafetyService() = default;
    
    bool authenticate() override;
    std::string aes_cbc_encrypt(const std::string& plaintext, const std::string& key);
    std::string generate_expire_time();
    bool authorize() override;
    std::string encryptMqttPassword(const std::string& mqttPassword,
                                   const std::string& productKey,
                                   const std::string& authCode = "") override;


private:
    //AuthService 
    //PermissionService
};