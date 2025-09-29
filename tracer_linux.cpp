#ifdef __linux__

#include "tracer_linux.h"

#include <elf.h>
#include <fcntl.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>

// Minimal AArch64 regs struct for GETREGSET
struct aarch64_regs {
    uint64_t x[31];
    uint64_t sp;
    uint64_t pc;
    uint64_t pstate;
};

static bool is_aarch64() {
#if defined(__aarch64__)
    return true;
#else
    return false;
#endif
}

static bool is_x86_64() {
#if defined(__x86_64__)
    return true;
#else
    return false;
#endif
}

struct SymbolInfo {
    std::string name;
    uint64_t vaddr; // st_value
};

static std::vector<SymbolInfo> read_elf_symbols(const std::string& path) {
    std::vector<SymbolInfo> out;
    std::ifstream f(path, std::ios::binary);
    if (!f) return out;

    unsigned char e_ident[EI_NIDENT] = {0};
    f.read(reinterpret_cast<char*>(e_ident), EI_NIDENT);
    if (!f) return out;
    bool is64 = (e_ident[EI_CLASS] == ELFCLASS64);
    bool le = (e_ident[EI_DATA] == ELFDATA2LSB);
    if (!is64 || !le) return out; // support 64-bit LE only

    Elf64_Ehdr eh{}; memcpy(&eh, e_ident, EI_NIDENT);
    f.read(reinterpret_cast<char*>(&eh) + EI_NIDENT, sizeof(Elf64_Ehdr) - EI_NIDENT);
    if (!f) return out;

    f.seekg(eh.e_shoff, std::ios::beg);
    std::vector<Elf64_Shdr> sh(eh.e_shnum);
    f.read(reinterpret_cast<char*>(sh.data()), eh.e_shentsize * eh.e_shnum);
    if (!f) return out;

    // Get section header string table
    std::string shstr;
    if (eh.e_shstrndx != SHN_UNDEF && eh.e_shstrndx < sh.size()) {
        auto& shstrhdr = sh[eh.e_shstrndx];
        shstr.resize(shstrhdr.sh_size);
        f.seekg(shstrhdr.sh_offset, std::ios::beg);
        f.read(shstr.data(), shstr.size());
    }

    // Find symtab/dynsym and their associated strtabs
    auto read_symbols_from = [&](const Elf64_Shdr& symhdr, const Elf64_Shdr& strhdr) {
        std::string strtab;
        strtab.resize(strhdr.sh_size);
        f.seekg(strhdr.sh_offset, std::ios::beg);
        f.read(strtab.data(), strtab.size());

        size_t nsyms = symhdr.sh_size / symhdr.sh_entsize;
        f.seekg(symhdr.sh_offset, std::ios::beg);
        for (size_t i = 0; i < nsyms; ++i) {
            Elf64_Sym sym{};
            f.read(reinterpret_cast<char*>(&sym), sizeof(sym));
            if (!f) break;
            if (sym.st_name == 0) continue;
            const char* nm = strtab.data() + sym.st_name;
            if (ELF64_ST_TYPE(sym.st_info) != STT_FUNC) continue;
            if (sym.st_value == 0) continue;
            out.push_back(SymbolInfo{nm, sym.st_value});
        }
    };

    for (size_t i = 0; i < sh.size(); ++i) {
        const Elf64_Shdr& hdr = sh[i];
        const char* name = shstr.empty() ? "" : (shstr.c_str() + hdr.sh_name);
        if (hdr.sh_type == SHT_SYMTAB || hdr.sh_type == SHT_DYNSYM) {
            // find linked strtab
            if (hdr.sh_link >= sh.size()) continue;
            const Elf64_Shdr& strhdr = sh[hdr.sh_link];
            if (strhdr.sh_type != SHT_STRTAB) continue;
            (void)name; // not used now
            read_symbols_from(hdr, strhdr);
        }
    }
    return out;
}

static std::optional<uint64_t> find_symbol_addr(const std::vector<SymbolInfo>& syms, const std::string& name) {
    for (const auto& s : syms) if (s.name == name) return s.vaddr;
    return std::nullopt;
}

static bool glob_match(const char* pat, const char* str) {
    const char *s = str, *p = pat;
    const char *star = nullptr, *ss = nullptr;
    while (*s) {
        if (*p == '?' || *p == *s) { ++s; ++p; continue; }
        if (*p == '*') { star = p++; ss = s; continue; }
        if (star) { p = star + 1; s = ++ss; continue; }
        return false;
    }
    while (*p == '*') ++p;
    return *p == '\0';
}

struct Breakpoint { uint64_t addr; uint64_t orig; size_t size; std::string name; std::optional<uint64_t> redirect; };

static uint64_t read_word(pid_t pid, uint64_t addr) {
    errno = 0;
    uint64_t val = ptrace(PTRACE_PEEKTEXT, pid, (void*)addr, 0);
    if (errno) return 0;
    return val;
}

static bool write_word(pid_t pid, uint64_t addr, uint64_t data) {
    return ptrace(PTRACE_POKETEXT, pid, (void*)addr, (void*)data) == 0;
}

static bool set_bp(pid_t pid, Breakpoint& bp) {
    bp.orig = read_word(pid, bp.addr);
    if (is_x86_64()) {
        uint64_t data = (bp.orig & ~0xFFull) | 0xCC; // INT3
        bp.size = 1;
        return write_word(pid, bp.addr, data);
    } else {
        // aarch64: write BRK 0 instruction (0xD4200000), 4 bytes aligned
        uint32_t brk = 0xD4200000u;
        uint64_t data = bp.orig;
        data &= ~0xFFFFFFFFull;
        data |= (uint64_t)brk;
        bp.size = 4;
        return write_word(pid, bp.addr, data);
    }
}

static bool restore_bp(pid_t pid, const Breakpoint& bp) {
    return write_word(pid, bp.addr, bp.orig);
}

static bool get_regs(pid_t pid, user_regs_struct& r) {
    return ptrace(PTRACE_GETREGS, pid, 0, &r) == 0;
}

static bool set_regs(pid_t pid, const user_regs_struct& r) {
    return ptrace(PTRACE_SETREGS, pid, 0, (void*)&r) == 0;
}

static bool get_regs_a64(pid_t pid, aarch64_regs& r) {
    iovec io{&r, sizeof(r)};
    return ptrace(PTRACE_GETREGSET, pid, (void*)NT_PRSTATUS, &io) == 0;
}

static bool set_regs_a64(pid_t pid, const aarch64_regs& r) {
    iovec io{(void*)&r, sizeof(r)};
    return ptrace(PTRACE_SETREGSET, pid, (void*)NT_PRSTATUS, &io) == 0;
}

static uint64_t get_pc(pid_t pid) {
    if (is_x86_64()) { user_regs_struct r{}; get_regs(pid, r); return r.rip; }
    aarch64_regs r{}; get_regs_a64(pid, r); return r.pc;
}

static void set_pc(pid_t pid, uint64_t pc) {
    if (is_x86_64()) { user_regs_struct r{}; get_regs(pid, r); r.rip = pc; set_regs(pid, r); return; }
    aarch64_regs r{}; get_regs_a64(pid, r); r.pc = pc; set_regs_a64(pid, r);
}

static uint64_t text_base_for(pid_t pid, const std::string& exe) {
    std::ifstream m("/proc/" + std::to_string(pid) + "/maps");
    std::string line;
    uint64_t best = 0;
    while (std::getline(m, line)) {
        // format: start-end perms offset dev inode pathname
        // find lines ending with exe path and perms contains 'x'
        if (line.rfind(exe, std::string::npos) == std::string::npos) continue;
        auto sp = line.find(' ');
        if (sp == std::string::npos) continue;
        std::string range = line.substr(0, sp);
        auto dash = range.find('-');
        if (dash == std::string::npos) continue;
        std::string start = range.substr(0, dash);
        uint64_t addr = std::stoull(start, nullptr, 16);
        if (!best || addr < best) best = addr;
    }
    return best;
}

static pid_t spawn_tracee(const TracerConfig& cfg, char* const* argv) {
    pid_t pid = fork();
    if (pid == 0) {
        ptrace(PTRACE_TRACEME, 0, 0, 0);
        execvp(cfg.program.c_str(), argv);
        _exit(127);
    }
    return pid;
}

int run_with_ptrace(const TracerConfig& cfg) {
    // Build argv
    std::vector<char*> av;
    av.push_back(const_cast<char*>(cfg.program.c_str()));
    for (auto& a : cfg.args) av.push_back(const_cast<char*>(a.c_str()));
    av.push_back(nullptr);

    pid_t pid = spawn_tracee(cfg, av.data());
    if (pid <= 0) { perror("fork/exec"); return -1; }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) { perror("waitpid"); return -1; }
    if (!(WIFSTOPPED(status) && WSTOPSIG(status) == SIGTRAP)) {
        std::cerr << "Tracee did not stop on SIGTRAP after exec" << std::endl;
    }

    // Resolve exe path
    char exepath[PATH_MAX];
    ssize_t n = readlink( (std::string("/proc/") + std::to_string(pid) + "/exe").c_str(), exepath, sizeof(exepath)-1);
    if (n < 0) { perror("readlink exe"); return -1; }
    exepath[n] = 0;
    std::string exe(exepath);

    // Read symbols from executable
    auto syms = read_elf_symbols(exe);
    if (syms.empty()) {
        std::cerr << "No symbols found in executable (stripped?)." << std::endl;
    }

    // Compute base address (PIE)
    uint64_t base = text_base_for(pid, exe);

    // Prepare breakpoints for each mapped source function present in the exe
    std::vector<Breakpoint> bps;
    for (const auto& kv : cfg.mapping) {
        const std::string& src = kv.first;
        const std::string& dst = kv.second;
        // find exact or pattern match among symbols to set bp at concrete names
        for (const auto& s : syms) {
            bool match = (src == s.name) || glob_match(src.c_str(), s.name.c_str());
            if (!match) continue;
            auto red = find_symbol_addr(syms, dst);
            Breakpoint bp{}; bp.name = s.name; bp.addr = base + s.vaddr; bp.redirect = red;
            bps.push_back(bp);
        }
    }

    // Install breakpoints
    for (auto& bp : bps) {
        if (!set_bp(pid, bp)) {
            std::cerr << "Failed to set breakpoint at 0x" << std::hex << bp.addr << std::dec << std::endl;
        }
    }

    // Continue the child
    ptrace(PTRACE_CONT, pid, 0, 0);

    while (true) {
        if (waitpid(pid, &status, 0) < 0) { perror("waitpid"); break; }
        if (WIFEXITED(status)) return WEXITSTATUS(status);
        if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
        if (!WIFSTOPPED(status)) continue;
        int sig = WSTOPSIG(status);

        if (sig == SIGTRAP) {
            uint64_t pc = get_pc(pid);
            // Find which bp hit (x86 advances PC by 1 on INT3; aarch64 stays at BRK)
            for (auto& bp : bps) {
                uint64_t hit = pc;
                if (is_x86_64()) {
                    if (pc != bp.addr + 1) continue;
                } else {
                    if (pc != bp.addr) continue;
                }
                // print wrapper message
                std::cerr << "wrapper to " << bp.name << std::endl;
                // restore original instruction
                restore_bp(pid, bp);

                if (bp.redirect && *bp.redirect != 0) {
                    // Redirect: set PC to target entry, do not execute original prologue
                    set_pc(pid, base + *bp.redirect);
                    // Reinsert breakpoint for next time at original site
                    set_bp(pid, bp);
                    ptrace(PTRACE_CONT, pid, 0, 0);
                } else {
                    // Step over original instruction at entry, then reinsert breakpoint
                    if (is_x86_64()) {
                        user_regs_struct r{}; get_regs(pid, r); r.rip = bp.addr; set_regs(pid, r);
                    }
                    ptrace(PTRACE_SINGLESTEP, pid, 0, 0);
                    waitpid(pid, &status, 0);
                    set_bp(pid, bp);
                    ptrace(PTRACE_CONT, pid, 0, 0);
                }
                goto contloop;
            }
        }
        // Pass through other signals
        ptrace(PTRACE_CONT, pid, 0, sig);
    contloop:
        ;
    }
    return -1;
}

#endif // __linux__

