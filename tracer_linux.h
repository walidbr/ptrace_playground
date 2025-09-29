#pragma once

#ifdef __linux__

#include <string>
#include <unordered_map>
#include <vector>

struct TracerConfig {
    std::string program;
    std::vector<std::string> args;
    std::unordered_map<std::string, std::string> mapping; // source -> target (same signature)
};

// Returns exit code of traced program or negative on error.
int run_with_ptrace(const TracerConfig& cfg);

#endif // __linux__

