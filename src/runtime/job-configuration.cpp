// Determine configuration changes and the environment published to Upstart.
#include "configuration.hpp"
#include "jobs.hpp"
#include "runtime-files.hpp"
#include <algorithm>
#include <cstdlib>

namespace kppmods {
namespace {
boost::json::array packageInputs(const Configuration &config, const Job &job,
                                 const std::string &name) {
    boost::json::array inputs;
    for (const auto &package : config.packages) {
        if (std::find(job.packages.begin(), job.packages.end(), package.id) ==
            job.packages.end()) {
            continue;
        }
        boost::json::object input{{"id", package.id},
                                  {"version", package.version},
                                  {"options", package.options},
                                  {"root", package.root}};
        if (name == "kppmainapp" && (!package.ui.empty() || package.ui_runtime)) {
            // UI consumers see package/app metadata, schemas and initial settings.
            input["settingsSchema"] = package.settingsSchema;
            if (auto settings = config.settings.if_contains(package.id)) {
                input["settings"] = *settings;
            }
        }
        inputs.push_back(std::move(input));
    }
    return inputs;
}
boost::json::array uiMetadata(const Configuration &config) {
    boost::json::array metadata;
    for (const auto &package : config.packages) {
        // Synchronous packages()/apps() APIs expose this snapshot to UI consumers.
        metadata.push_back(boost::json::object{{"id", package.id},
                                               {"version", package.version},
                                               {"title", package.title},
                                               {"root", package.root},
                                               {"apps", package.apps}});
    }
    return metadata;
}
} // namespace

std::set<std::string> affectedJobs(const Configuration &previous,
                                   const Configuration &candidate, bool force,
                                   const std::string &reloadPackageId) {
    std::set<std::string> jobs;
    const auto changes = [&](const auto &from, const auto &to) {
        for (const auto &[job, process] : from.processes) {
            auto other = to.processes.find(job);
            if ((force && (reloadPackageId.empty() ||
                           std::find(process.packages.begin(), process.packages.end(),
                                     reloadPackageId) != process.packages.end())) ||
                other == to.processes.end() || process != other->second ||
                packageInputs(from, process, job) !=
                    packageInputs(to, other->second, job)) {
                jobs.insert(job);
            }
        }
    };
    changes(previous, candidate);
    changes(candidate, previous);
    if ((previous.modules != candidate.modules ||
         uiMetadata(previous) != uiMetadata(candidate)) &&
        candidate.processes.count("kppmainapp")) {
        jobs.insert("kppmainapp");
    }
    if (jobs.count("statusbar") &&
        (!previous.ui_runtime.empty() || !candidate.ui_runtime.empty())) {
        jobs.insert("kppmainapp");
    }
    if (jobs.count("framework")) {
        jobs.insert("webreader");
        jobs.insert("kppmainapp");
    }
    return jobs;
}
std::map<std::string, std::string> jobEnvironment(const Configuration &configuration,
                                                  const std::string &job) {
    auto process = configuration.processes.find(job);
    Job empty;
    const auto &config =
        process == configuration.processes.end() ? empty : process->second;
    auto environment = config.env;
    std::string preload;
    for (const auto &library : config.preloads) {
        preload += (preload.empty() ? "" : ":") + library;
    }
    environment["LD_PRELOAD"] = preload;
    environment["LD_LIBRARY_PATH"] = "/var/local/kppmods/current/lib";
    environment["KPPMOD_CONFIG"] = configuration.root + "/config.json";
    environment["KPPMOD_PROCESS"] = job;
    environment["KPPMOD_STATUS_DIR"] = runRoot + "/native";
    if (job == "framework") {
        environment["KPPMOD_FRAMEWORK_PRELOAD"] = preload;
        if (!environment.count("JAVA_TOOL_OPTIONS") && getenv("JAVA_TOOL_OPTIONS")) {
            environment["JAVA_TOOL_OPTIONS"] = getenv("JAVA_TOOL_OPTIONS");
        }
    }
    return environment;
}
} // namespace kppmods
