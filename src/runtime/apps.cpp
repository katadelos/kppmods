// Discover the existing scriptlet launchers; packages own their scripts and icons.
#include "apps.hpp"
#include "configuration.hpp"
#include "process.hpp"
#include "runtime-files.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace kppmods {
namespace fs = std::filesystem;

Json installedApps() {
    boost::json::array result;
    for (const char *directory : {"/mnt/us/documents", "/mnt/us/applications"}) {
        std::error_code error;
        fs::directory_iterator files(directory, error), end;
        for (; !error && files != end; files.increment(error)) {
            const auto path = files->path();
            // The bootstrap script restarts this runtime; it is not an app.
            if (path == "/mnt/us/documents/kppmods.sh") {
                continue;
            }
            std::error_code typeError;
            if (path.extension() != ".sh" || !files->is_regular_file(typeError)) {
                continue;
            }
            std::ifstream script(path);
            if (!script) {
                continue;
            }
            std::string title = path.stem().string(), line;
            for (unsigned count = 0; count < 32 && std::getline(script, line);
                 ++count) {
                if (line.rfind("# Name:", 0)) {
                    continue;
                }
                auto first = line.find_first_not_of(" \t", 7),
                     last = line.find_last_not_of(" \t\r");
                if (first != std::string::npos && last >= first) {
                    title = line.substr(first, last - first + 1);
                }
                break;
            }
            std::string icon;
            for (const char *extension : {".png", ".svg"}) {
                auto image = path;
                image.replace_extension(extension);
                if (fs::is_regular_file(image, typeError)) {
                    icon = "file://" + image.string();
                    break;
                }
            }
            result.push_back(boost::json::object{{"id", path.string()},
                                                 {"script", path.string()},
                                                 {"title", title},
                                                 {"icon", icon}});
        }
    }
    std::sort(result.begin(), result.end(), [](const Json &left, const Json &right) {
        return left.at("title").as_string() < right.at("title").as_string();
    });
    return result;
}

void launchApp(const std::string &script) {
    if (geteuid()) {
        throw std::runtime_error("Launching applications requires root");
    }
    const auto apps = installedApps();
    const auto &entries = apps.as_array();
    if (std::none_of(entries.begin(), entries.end(), [&](const Json &app) {
            return app.at("script").as_string() == script;
        })) {
        throw std::runtime_error("Application script is no longer installed");
    }
    struct stat overlay{}, mounted{};
    const std::string jobs = "/var/run/kppmods-upstart";
    if (stat(jobs.c_str(), &overlay) || stat("/etc/upstart", &mounted) ||
        overlay.st_dev != mounted.st_dev || overlay.st_ino != mounted.st_ino ||
        !std::filesystem::exists(jobs + "/.kppmods")) {
        throw std::runtime_error("KPP Mods runtime service overlay is unavailable");
    }
    // No start event or respawn: only a user selection starts an application.
    // Upstart owns the process, independently of KPP and its command broker.
    const std::string job = "description \"KPP Mods application launcher\"\n"
                            "console none\n"
                            "script\n"
                            "  unset LD_PRELOAD KPPMOD_FRAMEWORK_PRELOAD\n"
                            "  cd \"$(dirname \"$APP\")\"\n"
                            "  exec /bin/sh \"$APP\" >> /var/run/kppmods/app.log 2>&1\n"
                            "end script\n";
    writeFileAtomically(jobs + "/kppmods-app.conf", job);
    if (runCommand({"/sbin/initctl", "reload-configuration"}) ||
        runCommand({"/sbin/initctl", "start", "kppmods-app", "APP=" + script})) {
        throw std::runtime_error(
            "Application could not start; another application may already be running");
    }
}
} // namespace kppmods
