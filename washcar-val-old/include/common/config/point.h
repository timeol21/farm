#pragma once
#include <string>
#include <cctype>

namespace washcar {
namespace cfg {

// 软元件地址的通用表示。业务代码层用,运维层不直接见这个 struct(运维只看到 "M26" 这种字符串)
struct Point {
    char type;        // 'D' / 'Y' / 'M' / 'X' / 'S',大写
    int  addr;        // 10 进制地址(项目惯例,虽然三菱 FX 实际是 8 进制)

    bool operator==(const Point& o) const { return type == o.type && addr == o.addr; }
};

// 把 "M26" 拆成 {'M', 26}。失败时返回 {'?', -1},业务代码要 check。
inline Point parsePoint(const std::string& text) {
    if (text.empty()) return {'?', -1};
    char type = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
    if (type != 'D' && type != 'Y' && type != 'M' && type != 'X' && type != 'S') return {'?', -1};
    try {
        int addr = std::stoi(text.substr(1));
        return {type, addr};
    } catch (...) {
        return {'?', -1};
    }
}

// 把 Point 拼成 "M26"。空 type 时返回空串。
inline std::string formatPoint(const Point& p) {
    if (p.type == '?' || p.addr < 0) return {};
    return std::string(1, p.type) + std::to_string(p.addr);
}
inline std::string formatPoint(char type, int addr) {
    return formatPoint(Point{type, addr});
}

}  // namespace cfg
}  // namespace washcar
