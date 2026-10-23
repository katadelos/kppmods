#include "apps.hpp"
#include "broker.hpp"
#include "jobs.hpp"
#include "packages.hpp"
#include "runtime-cache.hpp"
#include "userstore.hpp"
#include <algorithm>
#include <pwd.h>
#include <unistd.h>

namespace kppmods::runtime {
const Package &Broker::package(const std::string &id) const {
    const auto found = std::ranges::find(config.packages, id, &Manifest::id);
    if (found == config.packages.end()) {
        throw std::runtime_error("Unknown extension: " + id);
    }
    return *found;
}
void Broker::loadConfiguration() {
    // Retain the previously activated processes when rebuilding a deleted
    // cache, so reconciliation also removes obsolete injections.
    config = Configuration{};
    if (std::filesystem::exists(configPath)) {
        config = readJson(configPath, Configuration{});
    } else if (userstoreMounted()) {
        config = readJson(sourceState + "/config.json", Configuration{});
    }
    config.root = cacheState;
    data = config.root + "/data";
    std::filesystem::create_directories(data);
    const auto native = runRoot + "/native";
    std::filesystem::create_directories(runRoot + "/development");
    std::filesystem::create_directories(native);
    if (auto account = getpwnam("framework")) {
        if (chown(native.c_str(), account->pw_uid, account->pw_gid)) {
            throw std::runtime_error("Cannot set native diagnostics owner");
        }
    }
    pendingJobs = readJson(config.root + "/pending-jobs.json", std::set<std::string>{});
    disabled = readJson(config.root + "/disabled.json", Disabled{"fake-registration"});
    disabled.erase("ui-host");
    disabled.erase("mod-manager");
    config.settings.clear();
    for (const auto &p : config.packages) {
        if (!km::isPackageId(p.id)) {
            throw std::runtime_error("Invalid package ID");
        }
        config.settings[p.id] =
            km::files::readSettings(data + "/" + p.id + ".json", p.settings);
    }
}
bool Broker::managing() const {
    return !managementResults.empty() &&
           managementResults.rbegin()->second.wait_for(std::chrono::seconds(0)) !=
               std::future_status::ready;
}
Json Broker::manage(const Json &body) {
    const auto &root = config.root;
    const auto command = km::get<std::string>(body, "command");
    const auto argument = km::get<std::string>(body, "argument");
    static const std::map<std::string, bool> arguments = {{"configuration", false},
                                                          {"development-start", false},
                                                          {"development-stop", false},
                                                          {"status", false},
                                                          {"list", false},
                                                          {"apps", false},
                                                          {"reload", false},
                                                          {"reconcile", false},
                                                          {"apply", true},
                                                          {"enable", true},
                                                          {"disable", true},
                                                          {"launch-app", true}};
    const auto arity = arguments.find(command);
    if (arity == arguments.end() ||
        (command != "reload" && km::contains(body, "argument") != arity->second)) {
        throw std::runtime_error("Invalid command or argument; see --help");
    }
    if (command == "configuration") {
        return boost::json::value_from(config);
    }
    if (command == "status") {
        auto result = runtimeStatus(config, managing());
        result.as_object()["pending_jobs"] = boost::json::value_from(pendingJobs);
        return result;
    }
    auto admission = userstoreAdmission();
    if (command == "list") {
        boost::json::array result;
        const auto installed = installedPackages();
        const auto unavailable = unavailablePackages(installed);
        for (const auto &[id, p] : installed) {
            const bool available = !unavailable.count(id);
            result.push_back(boost::json::object{
                {"id", id},
                {"title", p.title.empty() ? id : p.title},
                {"enabled", available && !disabled.count(id) &&
                                std::ranges::find(config.packages, id, &Manifest::id) !=
                                    config.packages.end()},
                {"available", available},
                {"unavailableReason", available ? "" : unavailable.at(id)},
                {"needs", boost::json::value_from(p.needs)}});
        }
        return result;
    }
    if (command == "apps") {
        return installedApps();
    }
    if (managing()) {
        throw std::runtime_error("Another runtime operation is running");
    }
    if (config.development.is_object() && command != "launch-app" &&
        command != "development-stop" && command != "development-start" &&
        command != "reload") {
        throw std::runtime_error("Stop the development session before activation");
    }
    const auto previous = config;
    const auto pending = pendingJobs;
    const auto currentDisabled = command == "reconcile"
        ? readJson(root + "/disabled.json", disabled) : disabled;
    managementResults.emplace(
        ++managementId,
        std::async(
            std::launch::async,
            [this, root, command, argument, previous, currentDisabled,
             pending, admission = std::move(admission)]() mutable -> Json {
                // Release when this operation finishes, not when its retained
                // shared_future is eventually removed from the result cache.
                auto lease = std::move(admission);
                Json value;
                if (command == "launch-app") {
                    launchApp(argument);
                } else {
                    if (command == "reload") {
                        prepareRuntimeCache();
                    }
                    auto nextDisabled = currentDisabled;
                    const auto installed = installedPackages();
                    if (command == "apply") {
                        nextDisabled =
                            boost::json::value_to<Disabled>(km::parseJson(argument));
                        for (const auto &id : nextDisabled) {
                            if (!installed.count(id)) {
                                throw std::runtime_error(
                                    "Extension is not installed: " + id);
                            }
                        }
                        // Availability is separate from the user's saved switch.
                        // Reject attempts to turn on a previously disabled package
                        // while its software is missing; retain other preferences.
                        for (const auto &[id, reason] :
                             unavailablePackages(installed)) {
                            if (!nextDisabled.count(id) && currentDisabled.count(id)) {
                                throw std::runtime_error(reason);
                            }
                            if (currentDisabled.count(id)) {
                                nextDisabled.insert(id);
                            } else {
                                nextDisabled.erase(id);
                            }
                        }
                    } else if (command == "enable" || command == "disable") {
                        setPackageEnabled(installed, nextDisabled, argument,
                                          command == "enable");
                    } else if (!argument.empty() && !km::isPackageId(argument)) {
                        throw std::runtime_error("Invalid extension ID");
                    }
                    auto candidate = buildConfiguration(
                        root, enabledPackages(installed, nextDisabled));
                    if (command == "development-start" ||
                        (command == "reload" && previous.development.is_object())) {
                        const auto directory = root + "/development";
                        if (candidate.ui_runtime.empty() ||
                            !std::filesystem::is_regular_file(directory + "/host.js")) {
                            throw std::runtime_error("Development requires a UI "
                                                     "runtime and uploaded host.js");
                        }
                        if (command == "development-start") {
                            std::filesystem::remove(runRoot +
                                                    "/development/update.json");
                            std::filesystem::remove(runRoot +
                                                    "/development/result.json");
                        }
                        candidate.development = boost::json::object{{"pollMs", 200}};
                        for (auto &script :
                             candidate.processes.at("kppmainapp").scripts) {
                            if (script.at("id") == "kppmods.host") {
                                script.as_object()["path"] = directory + "/host.js";
                            }
                        }
                    }
                    auto jobs = affectedJobs(previous, candidate, command == "reload",
                                             argument);
                    jobs.insert(pending.begin(), pending.end());
                    {
                        std::lock_guard<std::mutex> guard(mutex);
                        saveJsonAtomically(root + "/pending-jobs.json",
                                           boost::json::value_from(jobs));
                        pendingJobs = jobs;
                        saveJsonAtomically(root + "/disabled.json",
                                           boost::json::value_from(nextDisabled));
                        saveJsonAtomically(configPath, boost::json::value_from(candidate));
                        disabled = std::move(nextDisabled);
                        if (config.packages != candidate.packages ||
                            config.processes != candidate.processes) {
                            statusReset = false;
                            nextStatusReset = 0;
                        }
                        config = candidate;
                    }
                    activate(candidate, std::move(jobs));
                    {
                        std::lock_guard<std::mutex> guard(mutex);
                        saveJsonAtomically(root + "/pending-jobs.json",
                                           boost::json::array{});
                        pendingJobs.clear();
                    }
                    value = "Reloaded";
                }
                return value;
            })
            .share());
    if (managementResults.size() > 32) {
        managementResults.erase(managementResults.begin());
    }
    return boost::json::object{{"operation", managementId}};
}
} // namespace kppmods::runtime
