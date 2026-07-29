#include "shell_exec.hpp"
#include <array>
#include <cctype>
#include <cstdio>
#include <memory>
#include <stdexcept>

namespace shell {

std::pair<int, std::string> run(const std::string& cmd) {
    std::string full = cmd + " 2>&1";
    std::array<char, 4096> buffer{};
    std::string result;

    FILE* pipe = popen(full.c_str(), "r");
    if (!pipe) {
        return {-1, "popen() failed"};
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result += buffer.data();
    }

    int status = pclose(pipe);
    int exit_code = -1;
    if (status != -1) {
#ifdef WIFEXITED
        if (WIFEXITED(status)) {
            exit_code = WEXITSTATUS(status);
        }
#else
        exit_code = status;
#endif
    }
    return {exit_code, result};
}

bool isSafeToken(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    for (unsigned char c : s) {
        if (!std::isalnum(c) && c != '_' && c != '-') return false;
    }
    return true;
}

}  // namespace shell
