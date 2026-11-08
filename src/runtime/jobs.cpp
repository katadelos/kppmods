// Restart existing Upstart jobs.
#include "jobs.hpp"
#include "common/lipc.hpp"
#include "configuration.hpp"
#include "process.hpp"
#include "runtime-files.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <climits>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <signal.h>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace kppmods {

bool jobLibrariesLoaded(const Configuration &configuration, const std::string &job) {
    std::vector<std::string> targets;
    if (job == "framework") {
        targets = {"cvm", "java"};
    } else if (job == "kppmainapp") {
        targets = {"KPPMainAppV1", "KPPMainAppV2", "KPPMainApp"};
    } else if (job == "webreader") {
        targets = {"webreader"};
    } else if (job == "statusbar") {
        targets = {"JunoStatusBarDriver"};
    } else {
        return true;
    }
    for (const auto &entry : std::filesystem::directory_iterator("/proc")) {
        const auto pid = entry.path().filename().string();
        if (!std::all_of(pid.begin(), pid.end(), ::isdigit)) {
            continue;
        }
        char b[PATH_MAX];
        ssize_t n = readlink(("/proc/" + pid + "/exe").c_str(), b, sizeof b);
        if (n < 1) {
            continue;
        }
        std::string exe(b, n);
        auto slash = exe.rfind('/');
        auto name = exe.substr(slash + 1);
        if (std::find(targets.begin(), targets.end(), name) == targets.end()) {
            continue;
        }
        std::ifstream f("/proc/" + pid + "/maps");
        std::string maps((std::istreambuf_iterator<char>(f)), {});
        bool ok = true;
        for (auto &lib : (configuration.processes.count(job)
                              ? configuration.processes.at(job).preloads
                              : Arguments{})) {
            if (maps.find(std::filesystem::canonical(lib).string()) ==
                std::string::npos) {
                ok = false;
            }
        }
        if (ok) {
            return true;
        }
    }
    return false;
}

// Only edit the writable runtime copy, never a stock rootfs job. Upstart reads
// these defaults on every start, including starts issued by other applications.
void publishJobEnvironment(const Configuration &configuration) {
    const std::string directory = "/var/run/kppmods-upstart";
    struct stat overlay{}, mounted{};
    if (stat(directory.c_str(), &overlay) || stat("/etc/upstart", &mounted) ||
        overlay.st_dev != mounted.st_dev || overlay.st_ino != mounted.st_ino ||
        !std::filesystem::exists(directory + "/.kppmods")) {
        throw std::runtime_error(
            "Runtime Upstart overlay is not mounted; run kppmods-start");
    }
    const std::string marker = "\n# KPP Mods runtime environment\n";
    bool changed = false;
    for (const char *job : {"framework", "webreader", "statusbar", "kppmainapp"}) {
        const auto path = directory + "/" + job + ".conf";
        auto contents = readFile(path);
        const auto start = contents.find(marker);
        if (start != std::string::npos) {
            contents.erase(start);
        }
        std::ostringstream defaults;
        defaults << marker;
        for (const auto &[key, value] : jobEnvironment(configuration, job)) {
            if (key.empty() ||
                key.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuv"
                                      "wxyz0123456789_") != std::string::npos ||
                (key.front() >= '0' && key.front() <= '9') ||
                value.find_first_of("\r\n") != std::string::npos) {
                throw std::runtime_error("Invalid Upstart environment entry: " + key);
            }
            defaults << "env " << key << '=' << std::quoted(value) << '\n';
        }
        contents += defaults.str();
        if (contents != readFile(path)) {
            writeFileAtomically(path, contents);
            changed = true;
        }
    }
    if (changed && runCommand({"/sbin/initctl", "reload-configuration"})) {
        throw std::runtime_error("Could not reload runtime Upstart configuration");
    }
}

// KPP's static destructors can crash during SIGTERM shutdown. Freeze only the
// process owned by this Upstart job, then stop the job before terminating it.
void stopKppForReload() {
    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::seconds(30);
    std::string state;
    for (;;) {
        state = captureCommand({"/sbin/initctl", "status", "kppmainapp"});
        if (state.find("stop/waiting") != std::string::npos) {
            return;
        }
        if (state.find("start/running") != std::string::npos) {
            break;
        }
        if (Clock::now() >= deadline) {
            throw std::runtime_error("KPP did not reach a stable state for reload");
        }
        sleep(1);
    }
    long process = 0;
    char extra;
    if (sscanf(state.c_str(), "kppmainapp start/running, process %ld %c",
               &process, &extra) != 1 || process <= 1 || process > INT_MAX) {
        throw std::runtime_error("Cannot identify Upstart's KPP process");
    }
    const std::string proc = "/proc/" + std::to_string(process);
    const auto executable = std::filesystem::read_symlink(proc + "/exe").filename();
    if (executable != "KPPMainApp" && executable != "KPPMainAppV1" &&
        executable != "KPPMainAppV2") {
        throw std::runtime_error("Upstart's KPP process is not the stock application");
    }
    if (kill(process, SIGSTOP)) {
        throw std::runtime_error("Cannot pause KPP for reload");
    }
    bool stopping = false;
    try {
        const auto pausedDeadline = Clock::now() + std::chrono::seconds(5);
        for (;;) {
            const auto stat = readFile(proc + "/stat");
            const auto name = stat.rfind(')');
            if (name != std::string::npos && name + 2 < stat.size() &&
                stat[name + 2] == 'T') {
                break;
            }
            if (Clock::now() >= pausedDeadline) {
                throw std::runtime_error("KPP did not pause for reload");
            }
            usleep(10000);
        }
        std::string flag, variable;
        std::istringstream environment(readFile(proc + "/environ"));
        const std::string prefix = "KPPMAINAPP_FLAG_FILE=";
        while (std::getline(environment, variable, '\0')) {
            if (variable.compare(0, prefix.size(), prefix) == 0) {
                flag = variable.substr(prefix.size());
                break;
            }
        }
        struct stat marker {};
        bool hasMarker = false;
        if (!flag.empty()) {
            const auto path = std::filesystem::path(flag);
            if (path != path.lexically_normal() ||
                (flag.compare(0, 5, "/tmp/") != 0 &&
                 flag.compare(0, 9, "/var/run/") != 0)) {
                throw std::runtime_error("KPP crash marker is outside its runtime directories");
            }
            hasMarker = lstat(flag.c_str(), &marker) == 0;
            if ((hasMarker && !S_ISREG(marker.st_mode)) ||
                (!hasMarker && errno != ENOENT)) {
                throw std::runtime_error("Cannot inspect KPP's crash marker");
            }
        }
        if (runCommand({"/sbin/initctl", "stop", "--no-wait", "kppmainapp"})) {
            throw std::runtime_error("Cannot stop KPP for reload");
        }
        stopping = true;
        // Clear only the marker read from this frozen process, if unchanged.
        if (hasMarker) {
            struct stat current {};
            if (lstat(flag.c_str(), &current) || current.st_dev != marker.st_dev ||
                current.st_ino != marker.st_ino || unlink(flag.c_str())) {
                throw std::runtime_error("KPP crash marker changed during reload");
            }
        }
        if (kill(process, SIGKILL) && errno != ESRCH) {
            throw std::runtime_error("Cannot terminate the stopped KPP process");
        }
        const auto stoppedDeadline = Clock::now() + std::chrono::seconds(30);
        while (captureCommand({"/sbin/initctl", "status", "kppmainapp"})
                   .find("stop/waiting") == std::string::npos) {
            if (Clock::now() >= stoppedDeadline) {
                throw std::runtime_error("KPP did not finish stopping for reload");
            }
            sleep(1);
        }
    } catch (...) {
        kill(process, stopping ? SIGKILL : SIGCONT);
        throw;
    }
}

void restartJob(const Configuration &configuration, const std::string &job) {
    const auto environment = jobEnvironment(configuration, job);
    std::cerr << "Restarting " << job << "\n";
    bool started = false;
    for (int i = 0; i < 90; ++i) {
        std::string state = captureCommand({"/sbin/initctl", "status", job});
        // Upstart restart retains the running job's configuration/environment.
        // A complete stop lets the replacement job definition take effect.
        if (state.find("start/running") != std::string::npos) {
            if (job == "kppmainapp") {
                stopKppForReload();
            } else if (runCommand({"/sbin/initctl", "stop", job})) {
                sleep(1);
                continue;
            }
        }
        std::vector<std::string> a = {"/sbin/initctl", "start", job};
        for (const auto &[key, value] : environment) {
            a.push_back(key + "=" + value);
        }
        if (runCommand(a) == 0) {
            started = true;
            break;
        }
        sleep(1);
    }
    if (!started) {
        throw std::runtime_error("Could not start Upstart job " + job);
    }
    if (job == "framework") {
        // The VM maps preloads long before Java startup finishes. Starting KPP
        // during that interval can push startup past the stock watchdog deadline.
        auto connection = km::ipc::connect();
        auto getInt =
            km::ipc::api().symbol<decltype(&LipcGetIntProperty)>("LipcGetIntProperty");
        bool ready = false;
        const auto deadline = time(nullptr) + 120;
        while (time(nullptr) < deadline) {
            int value = 0;
            if (getInt(connection.get(), "com.lab126.kaf", "frameworkStarted",
                       &value) == LIPC_OK &&
                value == 1) {
                ready = true;
                break;
            }
            sleep(1);
        }
        if (!ready) {
            throw std::runtime_error(
                "Framework did not finish startup; inspect /var/log/messages");
        }
    }
    for (int i = 0; i < 60; ++i) {
        if (jobLibrariesLoaded(configuration, job)) {
            return;
        }
        sleep(1);
    }
    throw std::runtime_error("Upstart job did not load its configured libraries: " +
                             job);
}

} // namespace kppmods
