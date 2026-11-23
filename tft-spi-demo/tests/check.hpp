#pragma once

//! A tiny test framework: `TEST(name) { ... }` registers a test; CHECK,
//! CHECK_EQ and CHECK_THROWS fail it with a message. Run every test with
//! `tft_tests`, or only those whose names contain a filter: `tft_tests text`.

#include <cstdint>
#include <exception>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace check {

struct TestCase {
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

struct Failure : std::exception {
    explicit Failure(std::string message) : message(std::move(message)) {}
    const char* what() const noexcept override { return message.c_str(); }
    std::string message;
};

// ---- printing values in failure messages ----

template <class T>
void print(std::ostream& os, const T& v);

template <class T, class = void>
struct is_streamable : std::false_type {};
template <class T>
struct is_streamable<T, std::void_t<decltype(std::declval<std::ostream&>() << std::declval<const T&>())>>
    : std::true_type {};

template <class T, class = void>
struct is_range : std::false_type {};
template <class T>
struct is_range<T, std::void_t<decltype(std::begin(std::declval<const T&>())), decltype(std::end(std::declval<const T&>()))>>
    : std::true_type {};

template <class A, class B>
void print(std::ostream& os, const std::pair<A, B>& p) {
    os << '(';
    print(os, p.first);
    os << ", ";
    print(os, p.second);
    os << ')';
}

template <class T>
void print(std::ostream& os, const std::optional<T>& v) {
    if (!v) {
        os << "None";
    } else {
        os << "Some(";
        print(os, *v);
        os << ')';
    }
}

template <class T>
void print(std::ostream& os, const T& v) {
    if constexpr (std::is_same_v<T, bool>) {
        os << (v ? "true" : "false");
    } else if constexpr (std::is_integral_v<T>) {
        os << +v;  // uint8_t as a number, not a character
    } else if constexpr (std::is_enum_v<T> && !is_streamable<T>::value) {
        os << +static_cast<std::underlying_type_t<T>>(v);
    } else if constexpr (std::is_convertible_v<const T&, std::string_view>) {
        os << '"' << std::string_view(v) << '"';
    } else if constexpr (is_streamable<T>::value) {
        os << v;
    } else if constexpr (is_range<T>::value) {
        os << '[';
        std::size_t n = 0;
        for (const auto& item : v) {
            if (n) os << ", ";
            if (++n > 24) {
                os << "...";
                break;
            }
            print(os, item);
        }
        os << ']';
    } else {
        os << "<value>";
    }
}

template <class T>
std::string show(const T& v) {
    std::ostringstream os;
    print(os, v);
    return os.str();
}

template <class... Args>
std::string message(const Args&... args) {
    std::ostringstream os;
    ((os << args), ...);
    return os.str();
}

inline std::string location(const char* file, int line) { return std::string(file) + ":" + std::to_string(line); }

inline void check(bool ok, const char* expr, const char* file, int line, const std::string& msg) {
    if (!ok) {
        throw Failure(location(file, line) + ": CHECK(" + expr + ") failed" + (msg.empty() ? "" : ": " + msg));
    }
}

template <class A, class B>
void check_eq(const A& a, const B& b, const char* ea, const char* eb, const char* file, int line,
              const std::string& msg) {
    if (!(a == b)) {
        throw Failure(location(file, line) + ": CHECK_EQ(" + ea + ", " + eb + ") failed" +
                      (msg.empty() ? "" : ": " + msg) + "\n    left:  " + show(a) + "\n    right: " + show(b));
    }
}

}  // namespace check

#define CHECK_CONCAT_(a, b) a##b
#define CHECK_CONCAT(a, b) CHECK_CONCAT_(a, b)

#define TEST(name)                                                              \
    static void name();                                                         \
    static const ::check::Registrar CHECK_CONCAT(name, _registrar){#name, name}; \
    static void name()

/// CHECK(condition, message parts...)
#define CHECK(cond, ...) ::check::check(static_cast<bool>(cond), #cond, __FILE__, __LINE__, ::check::message(__VA_ARGS__))

/// CHECK_EQ(actual, expected, message parts...)
#define CHECK_EQ(a, b, ...) ::check::check_eq((a), (b), #a, #b, __FILE__, __LINE__, ::check::message(__VA_ARGS__))

/// CHECK_THROWS(ExceptionType, expression): the expression must throw that type.
/// Evaluates to the caught exception, e.g. `auto e = CHECK_THROWS(SizeError, f());`
#define CHECK_THROWS(Type, expr)                                                                              \
    ([&]() -> Type {                                                                                          \
        try {                                                                                                 \
            (void)(expr);                                                                                     \
        } catch (const Type& caught) {                                                                        \
            return caught;                                                                                    \
        }                                                                                                     \
        throw ::check::Failure(::check::location(__FILE__, __LINE__) + ": " #expr " didn't throw " #Type);    \
    }())
