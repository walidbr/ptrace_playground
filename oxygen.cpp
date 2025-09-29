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
    std::string injection_lib_path;
    std::string lib_name;
    std::string json_path;
    std::string so_path;
    std::string preload_target_lib;

    std::vector<char*> program_argv_vec;
    int arg_idx = 1;

    // Parse optional -i argument for direct injection
    if (arg_idx < argc && std::string(argv[arg_idx]) == "-i") {
        if (arg_idx + 1 < argc) {
            injection_lib_path = argv[arg_idx + 1];
            arg_idx += 2;
        } else {
            std::cerr << "Usage: " << argv[0] << " [-i <injection_library_path>] <lib> <program> [args...]" << std::endl;
            return 1;
        }
    }

    if (arg_idx + 2 > argc) { // Need at least <lib> and <program>
        std::cerr << "Usage: " << argv[0] << " [-i <injection_library_path>] <lib> <program> [args...]" << std::endl;
        return 1;
    }

    lib_name = argv[arg_idx++];
    json_path = "injections/" + lib_name + "/" + lib_name + ".json";
    so_path = "injections/" + lib_name + "/lib" + lib_name + ".so";

    std::string preloadVar = "LD_PRELOAD";

    if (!injection_lib_path.empty()) {
        preload_target_lib = injection_lib_path;
    } else {
        preload_target_lib = wrapper_lib_path();
    }

    // Collect program arguments
    for (int i = arg_idx; i < argc; ++i) {
        program_argv_vec.push_back(argv[i]);
    }
    program_argv_vec.push_back(nullptr); // Null-terminate for execvp

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
        setenv(preloadVar.c_str(), preload_target_lib.c_str(), 1); 
        
        if (injection_lib_path.empty()) {
            // Only set REAL_LIB_PATH and WRAP_MAP if using the generic wrapper
            setenv("REAL_LIB_PATH", so_path.c_str(), 1);
            setenv("WRAP_MAP", json_path.c_str(), 1);
        }

        // Child: exec target
        execvp(program_argv_vec[0], program_argv_vec.data());
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
