#include "business_layer/safety/safety_service.h"
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <cstring>
#include <chrono>
#include <stdexcept>
#include <iostream>

bool SafetyService::authenticate(){
    return true;
}


bool SafetyService::authorize(){
    return true;
}

// --------- Base64 编码（OpenSSL BIO 方式，无需外部依赖） ---------
static std::string base64_encode(const unsigned char* data, int len) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* bmem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bmem);

    // 去掉换行
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);

    BIO_write(b64, data, len);
    BIO_flush(b64);

    BUF_MEM* bptr = nullptr;
    BIO_get_mem_ptr(b64, &bptr);

    std::string result(bptr->data, bptr->length);

    BIO_free_all(b64);
    return result;
}

// --------- AES-CBC 加密 ---------
std::string SafetyService::aes_cbc_encrypt(const std::string& plaintext, const std::string& key) 
{
    if (plaintext.empty() || key.empty()) {
        std::cerr << "aes_cbc_encrypt: plaintext or key is empty" << std::endl;
        return "";
    }

    // 固定 IV: "wumei-smart-open" (16字节)
    const unsigned char iv[16] = {
        'w','u','m','e','i','-','s','m','a','r','t','-','o','p','e','n'
    };

    // --- 密钥补齐到标准长度（16/24/32 字节） ---
    std::string adjustedKey = key;
    const EVP_CIPHER* cipher = nullptr;

    if (adjustedKey.size() <= 16) {
        adjustedKey.resize(16, '\0');       // 不足16字节末尾补 \0
        cipher = EVP_aes_128_cbc();
    } else if (adjustedKey.size() <= 24) {
        adjustedKey.resize(24, '\0');       // 不足24字节末尾补 \0
        cipher = EVP_aes_192_cbc();
    } else if (adjustedKey.size() <= 32) {
        adjustedKey.resize(32, '\0');       // 不足32字节末尾补 \0
        cipher = EVP_aes_256_cbc();
    } else {
        adjustedKey.resize(32);             // 超过32字节截断到32
        cipher = EVP_aes_256_cbc();
    }

    // 初始化上下文
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        std::cerr << "aes_cbc_encrypt: EVP_CIPHER_CTX_new failed" << std::endl;
        return "";
    }

    // 加密初始化（1 = encrypt），使用调整后的密钥
    if (EVP_EncryptInit_ex(ctx, cipher, nullptr,
                           (const unsigned char*)adjustedKey.data(), iv) != 1) {
        std::cerr << "aes_cbc_encrypt: EVP_EncryptInit_ex failed" << std::endl;
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }

    // 分配输出缓冲区（明文 + 一个块大小）
    int outLen = 0;
    int bufLen = plaintext.size() + EVP_MAX_BLOCK_LENGTH;
    auto* outBuf = new unsigned char[bufLen];

    // 第一步：加密（可能部分块）
    if (EVP_EncryptUpdate(ctx, outBuf, &outLen,
                          (const unsigned char*)plaintext.data(),
                          plaintext.size()) != 1) {
        std::cerr << "aes_cbc_encrypt: EVP_EncryptUpdate failed" << std::endl;
        delete[] outBuf;
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }

    // 第二步：完成加密 + PKCS 填充
    int finalLen = 0;
    if (EVP_EncryptFinal_ex(ctx, outBuf + outLen, &finalLen) != 1) {
        std::cerr << "aes_cbc_encrypt: EVP_EncryptFinal_ex failed" << std::endl;
        delete[] outBuf;
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }

    int totalLen = outLen + finalLen;

    // Base64 编码
    std::string result = base64_encode(outBuf, totalLen);

    // 清理
    delete[] outBuf;
    EVP_CIPHER_CTX_free(ctx);

    return result;
}


    // --------- 生成过期时间 ---------
    std::string SafetyService::generate_expire_time() {
        auto now = std::chrono::system_clock::now();
        auto expire = now + std::chrono::hours(23);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            expire.time_since_epoch()).count();
        return std::to_string(ms);
    }
    // --------- 按规范加密 MQTT 密码 ---------
std::string SafetyService::encryptMqttPassword(const std::string& mqttPassword,
                                                const std::string& productKey,
                                                const std::string& authCode) {
    std::string expireTime = generate_expire_time();

    std::string plaintext;
    if (authCode.empty()) {
        plaintext = mqttPassword + "&" + expireTime;
    } else {
        plaintext = mqttPassword + "&" + expireTime + "&" + authCode;
    }

    return aes_cbc_encrypt(plaintext, productKey);
}
