#pragma once

#include <charconv>
#include <chrono>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace util {

inline bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

inline std::string_view trim(std::string_view s) {
    while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
    while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
    return s;
}

/// Splits on runs of whitespace, dropping empty pieces.
inline std::vector<std::string_view> split_whitespace(std::string_view s) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && is_space(s[i])) ++i;
        const std::size_t start = i;
        while (i < s.size() && !is_space(s[i])) ++i;
        if (i > start) out.push_back(s.substr(start, i - start));
    }
    return out;
}

/// Parses the whole of `s` as a number, or nothing.
template <class T>
std::optional<T> parse(std::string_view s) {
    T value{};
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc() || end != s.data() + s.size() || s.empty()) {
        return std::nullopt;
    }
    return value;
}

/// `"text"` with quotes and backslashes escaped, for messages.
inline std::string quoted(std::string_view s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + '"';
}

/// A duration for humans: `850µs`, `12.3ms`, `1.25s`.
inline std::string format_duration(std::chrono::nanoseconds d) {
    char buf[32];
    const double ns = static_cast<double>(d.count());
    if (ns < 1e6) {
        std::snprintf(buf, sizeof buf, "%.0fus", ns / 1e3);
    } else if (ns < 1e9) {
        std::snprintf(buf, sizeof buf, "%.1fms", ns / 1e6);
    } else {
        std::snprintf(buf, sizeof buf, "%.2fs", ns / 1e9);
    }
    return buf;
}

}  // namespace util
