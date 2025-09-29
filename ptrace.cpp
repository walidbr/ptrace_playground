#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>

#ifdef __linux__
#include "tracer_linux.h"
#endif

static std::string wrapper_lib_path() {
    // Choose appropriate shared library extension by platform
#if defined(__APPLE__)
    return std::string("./libwrap.dylib");
#else
    return std::string("./libwrap.so");
#endif
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <program> [args...]" << std::endl;
        return 1;
    }

    // On Linux, support a ptrace mode to also work with static linking
#ifdef __linux__
    const char* mode = getenv("WRAP_MODE"); // "preload" or "ptrace" (default ptrace on Linux)
    if (!mode || std::string(mode) == "ptrace") {
        // Load mapping JSON into a minimal map for tracer
        std::unordered_map<std::string, std::string> mapping;
        const char* mapPath = getenv("WRAP_MAP");
        if (!mapPath) mapPath = "./function_map.json";
        // Simple parse identical to wrapper’s minimal parser
        std::ifstream in(mapPath);
        if (in) {
            std::stringstream buf; buf << in.rdbuf();
            std::string s = buf.str();
            size_t i = 0;
            while (true) {
                size_t k1 = s.find('"', i); if (k1 == std::string::npos) break;
                size_t k2 = s.find('"', k1+1); if (k2 == std::string::npos) break;
                std::string key = s.substr(k1+1, k2-k1-1);
                size_t c1 = s.find(':', k2); if (c1 == std::string::npos) break;
                size_t v1 = s.find('"', c1); if (v1 == std::string::npos) break;
                size_t v2 = s.find('"', v1+1); if (v2 == std::string::npos) break;
                std::string val = s.substr(v1+1, v2-v1-1);
                if (!key.empty()) mapping[key] = val;
                i = v2 + 1;
            }
        }

        TracerConfig cfg;
        cfg.program = argv[1];
        for (int i = 2; i < argc; ++i) cfg.args.emplace_back(argv[i]);
        cfg.mapping = std::move(mapping);
        int rc = run_with_ptrace(cfg);
        return rc < 0 ? 1 : rc;
    }
#endif

    std::string preloadVar = "LD_PRELOAD";
    std::string lib = wrapper_lib_path();

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }
    if (pid == 0) {
        // Child: set up interpose env and exec target
#if defined(__APPLE__)
        preloadVar = "DYLD_INSERT_LIBRARIES";
        setenv("DYLD_FORCE_FLAT_NAMESPACE", "1", 1);
#endif
        setenv(preloadVar.c_str(), lib.c_str(), 1);
        // Provide function mapping file to wrapper
        setenv("WRAP_MAP", "./function_map.json", 1);
        // Child: exec target
        std::vector<char*> child_argv;
        for (int i = 1; i < argc; ++i) child_argv.push_back(argv[i]);
        child_argv.push_back(nullptr);
        execvp(child_argv[0], child_argv.data());
        perror("execvp");
        _exit(127);
    } else {
        int status = 0;
        if (waitpid(pid, &status, 0) < 0) {
            perror("waitpid");
            return 1;
        }
        if (WIFEXITED(status)) return WEXITSTATUS(status);
        if (WIFSIGNALED(status)) {
            std::cerr << "Child terminated by signal " << WTERMSIG(status) << std::endl;
            return 128 + WTERMSIG(status);
        }
        return 1;
    }
}
