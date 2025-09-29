#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>

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
