#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

// 本地配置台登录。只服务局域网/单盒,不做多用户 RBAC。
// 密码用 PBKDF2-SHA256 存 config.json 的 admin.passwordHash;
// 为空时使用内置默认密码 washcar,页面会提示修改。
class AuthService {
public:
    AuthService();

    bool login(const std::string& username, const std::string& password,
               std::string& token, std::string& error);
    bool validate(const std::string& token, std::string& username) const;
    void logout(const std::string& token);

    bool usingDefaultPassword() const { return m_defaultPassword; }
    bool verifyPassword(const std::string& password) const;
    static std::string makePasswordHash(const std::string& password);
    void setPasswordHashInMemory(const std::string& hash);

    static std::string extractToken(const std::string& cookieHeader);
    static std::string sessionCookie(const std::string& token, bool clear = false);

private:
    struct Session {
        std::string username;
        std::chrono::steady_clock::time_point expires;
    };

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, Session> m_sessions;
    std::string m_passwordHash;
    bool m_defaultPassword = false;
};
