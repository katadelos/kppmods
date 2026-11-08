// Publish one validated runtime configuration. Restart failures are reported, not
// reversed.
#include "configuration.hpp"
#include "jobs.hpp"
#include "process.hpp"
#include <stdexcept>
#include <unistd.h>
namespace kppmods {
void activate(const Configuration &candidate, std::set<std::string> jobs) {
    publishJobEnvironment(candidate);
    // The stock KPP post-stop hook reports every exit to appmgrd as a crash.
    // Restart the app manager with KPP so intentional reloads cannot enter its
    // crash recovery path or leave it holding the terminated app's context.
    const bool restartAppManager = jobs.count("kppmainapp");
    const bool appManagerRunning =
        restartAppManager &&
        captureCommand({"/sbin/initctl", "status", "appmgrd"}).find("start/running") !=
            std::string::npos;
    // Let firmware select its Home implementation. KPP_HOME is incomplete on
    // 5.14.1, where the working Home view is still provided by the Java booklet.
    std::string resume = "app://com.lab126.booklet.home";
    if (appManagerRunning) {
        const auto context = captureCommand(
            {"/usr/bin/lipc-get-prop", "com.lab126.appmgrd", "activeContext"});
        if (context.find("KPP_SETTINGS") != std::string::npos) {
            const auto start = context.find("app://");
            if (start != std::string::npos) {
                resume = context.substr(start, context.find('\n', start) - start);
            }
        }
        if (runCommand({"/sbin/initctl", "stop", "appmgrd"})) {
            throw std::runtime_error("Could not stop appmgrd for reload");
        }
    }
    try {
        // Stop KPP before a framework restart can send it SIGTERM indirectly.
        if (restartAppManager) {
            stopKppForReload();
        }
        for (const char *job : {"framework", "webreader", "statusbar", "kppmainapp"}) {
            if (jobs.erase(job)) {
                restartJob(candidate, job);
            }
        }
        for (const auto &job : jobs) {
            restartJob(candidate, job);
        }
    } catch (...) {
        if (restartAppManager) {
            runCommand({"/sbin/initctl", "start", "appmgrd"});
        }
        throw;
    }
    if (restartAppManager) {
        if (runCommand({"/sbin/initctl", "start", "appmgrd"})) {
            throw std::runtime_error("Could not restart appmgrd");
        }
        bool ready = false;
        for (int i = 0; i < 60; ++i) {
            if (captureCommand({"/usr/bin/lipc-probe", "com.lab126.KPPMainApp"})
                    .find("\tgo") != std::string::npos) {
                ready = true;
                break;
            }
            sleep(1);
        }
        if (!ready) {
            throw std::runtime_error("KPP did not register with LIPC after reload");
        }
        if (runCommand(
                {"/usr/bin/lipc-set-prop", "com.lab126.appmgrd", "start", resume})) {
            throw std::runtime_error("Could not reopen Kindle interface after reload");
        }
    }
}
} // namespace kppmods
