#include "business_layer/safety/auth_service.h"
#include "common/config/config_manager.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <cstdio>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace {

constexpr int kIterations = 100000;
constexpr int kSaltBytes = 16;
constexpr int kHashBytes = 32;
constexpr int kSessionHours = 8;
const char* const kDefaultPassword = "washcar";

std::string toHex(const unsigned char* data, int len) {
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < len; ++i) ss << std::setw(2) << static_cast<int>(data[i]);
    return ss.str();
}

bool fromHex(const std::string& hex, std::vector<unsigned char>& out) {
    if (hex.size() % 2 != 0) return false;
    out.clear();
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        unsigned int value = 0;
        std::istringstream ss(hex.substr(i, 2));
        ss >> std::hex >> value;
        if (ss.fail()) return false;
        out.push_back(static_cast<unsigned char>(value));
    }
    return true;
}

bool pbkdf2(const std::string& password, const std::vector<unsigned char>& salt,
            unsigned char* out, int outLen) {
    return PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()),
                             salt.data(), static_cast<int>(salt.size()),
                             kIterations, EVP_sha256(), outLen, out) == 1;
}

std::string randomHex(int bytes) {
    std::vector<unsigned char> buf(static_cast<std::size_t>(bytes));
    if (RAND_bytes(buf.data(), bytes) != 1) return {};
    return toHex(buf.data(), bytes);
}

}  // namespace

AuthService::AuthService() {
    m_passwordHash = ConfigManager::instance().admin().passwordHash;
    m_defaultPassword = m_passwordHash.empty();
    if (m_defaultPassword) {
        std::cerr << "[Auth] admin.passwordHash 为空,使用默认密码 '" << kDefaultPassword
                  << "',请登录后尽快修改" << std::endl;
    }
}

std::string AuthService::makePasswordHash(const std::string& password) {
    std::vector<unsigned char> salt(static_cast<std::size_t>(kSaltBytes));
    if (RAND_bytes(salt.data(), kSaltBytes) != 1) return {};
    unsigned char hash[kHashBytes];
    if (!pbkdf2(password, salt, hash, kHashBytes)) return {};
    return "pbkdf2_sha256$" + std::to_string(kIterations) + "$" +
           toHex(salt.data(), kSaltBytes) + "$" + toHex(hash, kHashBytes);
}

bool AuthService::verifyPassword(const std::string& password) const {
    const std::string& stored = m_passwordHash.empty()
        ? std::string("DEFAULT") : m_passwordHash;
    if (m_passwordHash.empty()) {
        return password == kDefaultPassword;
    }

    // pbkdf2_sha256$iter$salt$hash
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= stored.size()) {
        const std::size_t pos = stored.find('$', start);
        parts.push_back(stored.substr(start, pos == std::string::npos ? std::string::npos : pos - start));
        if (pos == std::string::npos) break;
        start = pos + 1;
    }
    if (parts.size() != 4 || parts[0] != "pbkdf2_sha256") return false;
    int iterations = 0;
    try { iterations = std::stoi(parts[1]); } catch (...) { return false; }
    if (iterations <= 0 || iterations > 1000000) return false;

    std::vector<unsigned char> salt;
    std::vector<unsigned char> expected;
    if (!fromHex(parts[2], salt) || !fromHex(parts[3], expected)) return false;
    if (expected.size() != kHashBytes) return false;

    unsigned char actual[kHashBytes];
    if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()),
                          salt.data(), static_cast<int>(salt.size()),
                          iterations, EVP_sha256(), kHashBytes, actual) != 1) {
        return false;
    }
    return CRYPTO_memcmp(actual, expected.data(), kHashBytes) == 0;
}

void AuthService::setPasswordHashInMemory(const std::string& hash) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_passwordHash = hash;
    m_defaultPassword = hash.empty();
}

bool AuthService::login(const std::string& username, const std::string& password,
                        std::string& token, std::string& error) {
    const auto& admin = ConfigManager::instance().admin();
    if (username != admin.username) {
        error = "用户名或密码错误";
        return false;
    }
    // 还没改过密码时,留空等价于默认密码 washcar;改过密码后留空必须失败。
    std::string effectivePassword = password;
    if (effectivePassword.empty() && m_passwordHash.empty()) {
        effectivePassword = kDefaultPassword;
    }
    if (!verifyPassword(effectivePassword)) {
        error = "用户名或密码错误";
        return false;
    }

    token = randomHex(24);
    if (token.empty()) {
        error = "生成会话失败";
        return false;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sessions[token] = Session{username, std::chrono::steady_clock::now() + std::chrono::hours(kSessionHours)};
    return true;
}

bool AuthService::validate(const std::string& token, std::string& username) const {
    if (token.empty()) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_sessions.find(token);
    if (it == m_sessions.end()) return false;
    if (it->second.expires < std::chrono::steady_clock::now()) return false;
    username = it->second.username;
    return true;
}

void AuthService::logout(const std::string& token) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sessions.erase(token);
}

std::string AuthService::extractToken(const std::string& cookieHeader) {
    const std::string key = "washcar_session=";
    const std::size_t pos = cookieHeader.find(key);
    if (pos == std::string::npos) return {};
    const std::size_t begin = pos + key.size();
    std::size_t end = cookieHeader.find(';', begin);
    if (end == std::string::npos) end = cookieHeader.size();
    return cookieHeader.substr(begin, end - begin);
}

std::string AuthService::sessionCookie(const std::string& token, bool clear) {
    if (clear) {
        return "Set-Cookie: washcar_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0";
    }
    return "Set-Cookie: washcar_session=" + token + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=28800";
}
