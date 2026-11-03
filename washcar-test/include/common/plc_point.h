#pragma once

// 跨层点值表：软元件名 -> 当前值。
//
// 放 common 而不是 data_layer，是为了让业务层能在"完全不认识 PLC 类型"的前提下
// 使用点位数据。业务层 include 这个头即可，不需要再碰 IPlcInstance / 状态容器。
//
// key 用 PLC 文档里的写法，统一大写："M187" / "D142" / "S20" / "X45" / "Y74"。

#include <cstddef>
#include <string>
#include <unordered_map>

struct PlcPointValues {
    std::unordered_map<std::string, int> values;

    bool has(const std::string& point) const { return values.count(point) > 0; }

    /// 取不到就返回 fallback。业务层判状态时"点没采到"和"值为 0"要能区分，
    /// 所以判断存在性请先用 has()。
    int at(const std::string& point, int fallback = 0) const {
        const auto it = values.find(point);
        return it == values.end() ? fallback : it->second;
    }

    void set(const std::string& point, int value) { values[point] = value; }

    /// 按"位点"取布尔值。点位不存在返回 false（区别于"值为 0"）
    bool getBit(const std::string& point, bool& out) const {
        const auto it = values.find(point);
        if (it == values.end()) {
            return false;
        }
        out = it->second != 0;
        return true;
    }

    /// 按"字点"（D 寄存器）取整数值
    bool getWord(const std::string& point, int& out) const {
        const auto it = values.find(point);
        if (it == values.end()) {
            return false;
        }
        out = it->second;
        return true;
    }

    /// 按软元件类型 + 地址取。等价于先 formatPoint 再查，
    /// 让调用方少写字符串拼接。
    bool getBit(char type, int addr, bool& out) const {
        return getBit(std::string(1, type) + std::to_string(addr), out);
    }

    bool getWord(char type, int addr, int& out) const {
        return getWord(std::string(1, type) + std::to_string(addr), out);
    }

    /// D 寄存器在 PLC 里是 16 位无符号，这里做一次收窄
    bool getWord(char type, int addr, uint16_t& out) const {
        int value = 0;
        if (!getWord(std::string(1, type) + std::to_string(addr), value)) {
            return false;
        }
        out = static_cast<uint16_t>(value);
        return true;
    }

    void clear() { values.clear(); }
    bool empty() const { return values.empty(); }
    std::size_t size() const { return values.size(); }
};
