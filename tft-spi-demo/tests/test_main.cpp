#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

#include "check.hpp"

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int passed = 0, failed = 0;
    const auto start = std::chrono::steady_clock::now();

    // Like `cargo test`: what a test prints is only shown if it fails.
    std::streambuf* const real_cout = std::cout.rdbuf();
    for (const auto& test : check::registry()) {
        if (filter && std::strstr(test.name, filter) == nullptr) {
            continue;
        }
        std::ostringstream captured;
        std::cout.rdbuf(captured.rdbuf());
        std::string failure;
        try {
            test.fn();
        } catch (const check::Failure& f) {
            failure = f.message;
        } catch (const std::exception& e) {
            failure = std::string("threw: ") + e.what();
        }
        std::cout.rdbuf(real_cout);

        if (failure.empty()) {
            ++passed;
        } else {
            ++failed;
            std::cout << "FAIL " << test.name << "\n  " << failure << "\n";
            if (!captured.str().empty()) {
                std::cout << "  ---- output ----\n" << captured.str() << "  ----------------\n";
            }
        }
    }

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    std::cout << "\n" << passed << " passed, " << failed << " failed (" << ms.count() << " ms)\n";
    return failed == 0 && passed > 0 ? 0 : 1;
}
