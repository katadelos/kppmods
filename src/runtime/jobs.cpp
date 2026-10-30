// Restart existing Upstart jobs.
#include "jobs.hpp"
#include "common/lipc.hpp"
#include "configuration.hpp"
#include "process.hpp"
#include "runtime-files.hpp"
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <signal.h>
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

void restartJob(const Configuration &configuration, const std::string &job) {
    const auto environment = jobEnvironment(configuration, job);
    std::cerr << "Restarting " << job << "\n";
    bool started = false;
    for (int i = 0; i < 90; ++i) {
        std::string state = captureCommand({"/sbin/initctl", "status", job});
        // Upstart restart retains the running job's configuration/environment.
        // A complete stop lets the replacement job definition take effect.
        if (state.find("start/running") != std::string::npos &&
            runCommand({"/sbin/initctl", "stop", job})) {
            sleep(1);
            continue;
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
