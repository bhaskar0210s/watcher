#include "processes.hpp"

#include <fcntl.h>
#include <libproc.h>
#include <pthread/spawn.h>
#include <signal.h>
#include <spawn.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace watcher {
namespace {

// Runs a tool at utility QoS and returns its stdout.
std::string run(const char* const argv[]) {
    int fds[2];
    if (pipe(fds) != 0) return {};

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_CLOEXEC_DEFAULT);
    posix_spawnattr_set_qos_class_np(&attr, QOS_CLASS_UTILITY);

    // Fixed environment: C locale keeps numbers like "12.5" parseable.
    char* const env[] = {const_cast<char*>("LC_ALL=C"), const_cast<char*>("PATH=/usr/bin:/bin"), nullptr};
    pid_t pid = 0;
    const int rc = posix_spawn(&pid, argv[0], &actions, &attr, const_cast<char* const*>(argv), env);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    close(fds[1]);

    std::string out;
    if (rc == 0) {
        char buf[16384];
        for (;;) {
            const ssize_t n = read(fds[0], buf, sizeof(buf));
            if (n > 0) {
                out.append(buf, size_t(n));
            } else if (n == 0 || errno != EINTR) {
                break;
            }
        }
        int status;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
    }
    close(fds[0]);
    return out;
}

const char* skipSpaces(const char* p) {
    while (*p == ' ' || *p == '\t') ++p;
    return p;
}

std::string restOfLine(const char* p, const char* end) {
    p = skipSpaces(p);
    while (end > p && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) --end;
    return std::string(p, size_t(end - p));
}

// Calls fn(lineStart, lineEnd) for each line of text.
template <typename Fn>
void forEachLine(const std::string& text, Fn fn) {
    const char* p = text.c_str();
    const char* end = p + text.size();
    while (p < end) {
        const char* nl = static_cast<const char*>(memchr(p, '\n', size_t(end - p)));
        const char* lineEnd = nl ? nl : end;
        fn(p, lineEnd);
        p = lineEnd + 1;
    }
}

// top prints sizes like "1760K", "775M+", "2.1G".
double parseSize(const char* token) {
    char* unit = nullptr;
    double value = strtod(token, &unit);
    switch (unit ? *unit : 0) {
        case 'K': return value * 1024.0;
        case 'M': return value * 1024.0 * 1024.0;
        case 'G': return value * 1024.0 * 1024.0 * 1024.0;
        case 'T': return value * 1024.0 * 1024.0 * 1024.0 * 1024.0;
        default: return value;
    }
}

bool canInspect(pid_t pid, uint64_t* footprint) {
    rusage_info_v0 info;
    if (proc_pid_rusage(pid, RUSAGE_INFO_V0, reinterpret_cast<rusage_info_t*>(&info)) != 0) return false;
    if (footprint) *footprint = info.ri_phys_footprint;
    return true;
}

bool stillRunning(pid_t pid) {
    return kill(pid, 0) == 0 || errno == EPERM;
}

void sortDescending(std::vector<ProcessUsage>& rows) {
    std::sort(rows.begin(), rows.end(), [](const ProcessUsage& a, const ProcessUsage& b) { return a.value > b.value; });
}

}  // namespace

std::vector<ProcessUsage> topCpuProcesses(size_t count) {
    const char* const argv[] = {"/bin/ps", "-Aceo", "pid,pcpu,comm", "-r", nullptr};
    const std::string out = run(argv);
    std::vector<ProcessUsage> rows;
    bool header = true;
    forEachLine(out, [&](const char* p, const char* end) {
        if (header) {
            header = false;
            return;
        }
        if (rows.size() >= count || p == end) return;
        char* next = nullptr;
        const long pid = strtol(p, &next, 10);
        if (next == p) return;
        const char* cpuStart = next;
        const double cpu = strtod(cpuStart, &next);
        if (next == cpuStart) return;
        rows.push_back({pid_t(pid), restOfLine(next, end), cpu});
    });
    return rows;
}

std::vector<ProcessUsage> foreignMemorySnapshot(size_t count) {
    const std::string limit = std::to_string(count);
    const char* const argv[] = {"/usr/bin/top", "-l", "1", "-o", "mem", "-n", limit.c_str(), "-stats", "pid,mem,command", nullptr};
    const std::string out = run(argv);
    std::vector<ProcessUsage> rows;
    bool inTable = false;
    forEachLine(out, [&](const char* p, const char* end) {
        p = skipSpaces(p);
        if (!inTable) {
            inTable = strncmp(p, "PID", 3) == 0;
            return;
        }
        char* next = nullptr;
        const long pid = strtol(p, &next, 10);
        if (next == p || pid < 0) return;
        const char* sizeStart = skipSpaces(next);
        const char* sizeEnd = sizeStart;
        while (sizeEnd < end && *sizeEnd != ' ') ++sizeEnd;
        const std::string sizeToken(sizeStart, size_t(sizeEnd - sizeStart));
        // Our own processes are read live; keep only the ones we can't inspect.
        if (canInspect(pid_t(pid), nullptr)) return;
        rows.push_back({pid_t(pid), restOfLine(sizeEnd, end), parseSize(sizeToken.c_str())});
    });
    return rows;
}

std::vector<ProcessUsage> topMemoryProcesses(size_t count, const std::vector<ProcessUsage>& foreign) {
    int n = proc_listallpids(nullptr, 0);
    if (n <= 0) return {};
    std::vector<pid_t> pids(size_t(n) + 64);
    n = proc_listallpids(pids.data(), int(pids.size() * sizeof(pid_t)));
    if (n <= 0) return {};
    pids.resize(std::min(pids.size(), size_t(n)));

    std::vector<ProcessUsage> rows;
    rows.reserve(pids.size());
    for (pid_t pid : pids) {
        uint64_t footprint = 0;
        if (pid > 0 && canInspect(pid, &footprint)) rows.push_back({pid, {}, double(footprint)});
    }
    for (const ProcessUsage& row : foreign) {
        if (stillRunning(row.pid)) rows.push_back(row);
    }
    sortDescending(rows);
    if (rows.size() > count) rows.resize(count);

    for (ProcessUsage& row : rows) {
        if (!row.name.empty()) continue;
        char name[2 * MAXCOMLEN + 1] = {};
        if (proc_name(row.pid, name, sizeof(name)) > 0) {
            row.name = name;
        } else {
            char path[PROC_PIDPATHINFO_MAXSIZE] = {};
            if (proc_pidpath(row.pid, path, sizeof(path)) > 0) {
                const char* slash = strrchr(path, '/');
                row.name = slash ? slash + 1 : path;
            }
        }
    }
    return rows;
}

}  // namespace watcher
