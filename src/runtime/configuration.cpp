// Compose runtime configuration from installed extensions and device options.
#include "configuration.hpp"
#include "device.hpp"
#include "hermes/discovery.hpp"
#include "packages.hpp"
#include <algorithm>

namespace kppmods {

Json mergeOptions(Json defaults, const Json &overrides) {
    if (!overrides.is_object()) {
        throw std::runtime_error("Package options must be an object");
    }
    if (!defaults.is_object()) {
        defaults = boost::json::object{};
    }
    auto &options = defaults.as_object();
    for (const auto &entry : overrides.as_object()) {
        auto &current = options[entry.key()];
        current = entry.value().is_object() && current.is_object()
                      ? mergeOptions(current, entry.value())
                      : entry.value();
    }
    return defaults;
}

Configuration buildConfiguration(const std::string &installationRoot,
                                 const std::vector<Package> &packages) {
    Configuration c;
    c.firmware = readFirmware(installationRoot);
    c.root = installationRoot;
    const auto options = readJson(installationRoot + "/options.json",
                                  std::map<std::string, boost::json::object>{});
    const bool hasUi =
        std::any_of(packages.begin(), packages.end(), [](const Package &p) {
            return p.ui_runtime.has_value() || !p.ui.empty();
        });
    if (hasUi) {
        c.modules = km::hermes::discover("/app/KPPMainApp/js/KPPMainApp.js.hbc");
    }
    const auto base = readJson(installationRoot + "/base.json", Configuration{});
    std::map<std::string, std::vector<std::string>> preloads;
    for (const auto &[job, process] : base.processes) {
        preloads[job] = process.preloads;
    }
    std::set<std::string> hostJobs;
    const Package *provider = nullptr;
    for (const auto &p : packages) {
        Package manifest = p;
        if (auto override = options.find(p.id); override != options.end()) {
            manifest.options = mergeOptions(p.options, override->second).as_object();
        }
        c.packages.push_back(std::move(manifest));
        c.settings[p.id] = km::files::readSettings(
            installationRoot + "/data/" + p.id + ".json", p.settings);
        for (const auto &[job, libraries] : p.native) {
            for (const auto &library : libraries) {
                std::string path;
                if (const auto *file = std::get_if<std::string>(&library)) {
                    path = *file;
                } else {
                    const auto &variants = std::get<NativeVariants>(library);
                    const auto abi = detectStringAbi(variants.abi);
                    if (!abi) {
                        continue;
                    }
                    path = variants.variants.at(*abi);
                }
                preloads[job].push_back(packagePath(p.root, path));
            }
        }
        for (const auto &module : p.modules) {
            hostJobs.insert(module.process);
            c.processes[module.process].plugins.push_back(
                boost::json::object{{"id", p.id + "." + module.id},
                                    {"path", packagePath(p.root, module.path)},
                                    {"root", p.root}});
        }
        for (const auto &entry : p.ui) {
            c.processes["kppmainapp"].scripts.push_back(boost::json::object{
                {"id", p.id}, {"path", packagePath(p.root, entry)}});
        }
        for (const auto &entry : p.statusbar) {
            c.processes["statusbar"].statusbar.push_back(
                boost::json::object{{"id", p.id + ":" + entry.id},
                                    {"icon", packagePath(p.root, entry.icon)},
                                    {"uri", entry.uri}});
        }
        if (p.ui_runtime) {
            if (provider) {
                throw std::runtime_error(
                    "Multiple UI runtime providers: " + provider->id + " and " + p.id);
            }
            provider = &p;
        }
    }
    if (hasUi) {
        if (!provider) {
            throw std::runtime_error(
                "UI extensions require exactly one ui_runtime provider");
        }
        hostJobs.insert("kppmainapp");
        auto &scripts = c.processes["kppmainapp"].scripts;
        scripts.insert(
            scripts.begin(),
            boost::json::object{
                {"id", "kppmods.toolkit"},
                {"path", packagePath(provider->root, provider->ui_runtime->toolkit)}});
        scripts.insert(
            scripts.begin(),
            boost::json::object{
                {"id", "kppmods.host"},
                {"path", packagePath(provider->root, provider->ui_runtime->host)}});
        c.ui_runtime = provider->id;
    }
    for (const auto &job : hostJobs) {
        // Probe the library that defines the hook; the executable may not
        // import it (for example on firmware 5.14.2).
        const auto variant =
            job == "kppmainapp"
                ? detectStringAbi({"/app/lib/libReactNative.so",
                                   "_ZN11reactNative6Bridge10loadBundleERKSs",
                                   "_ZN11reactNative6Bridge10loadBundleERKNSt7__"
                                   "cxx1112basic_stringIcSt11char_traitsIcESaIcEEE"})
                      .value()
                : "0";
        preloads[job].insert(preloads[job].begin(),
                             runtimeRoot + "/abi" + variant + "/libkppmods-host.so");
    }
    for (auto &[job, libraries] : preloads) {
        auto &process = c.processes[job];
        if (const auto found = base.processes.find(job);
            found != base.processes.end()) {
            process.env = found->second.env;
        }
        if (job == "framework") {
            libraries.insert(libraries.begin(),
                             runtimeRoot + "/libkppmods-framework-exec.so");
        }
        std::set<std::string> unique;
        for (const auto &library : libraries) {
            if (library.find_first_of(" \t\r\n:") != std::string::npos) {
                throw std::runtime_error(
                    "Preload path contains an LD_PRELOAD separator: " + library);
            }
            if (unique.insert(library).second) {
                if (!std::filesystem::exists(library)) {
                    throw std::runtime_error("Missing loader/library " + library);
                }
                process.preloads.push_back(library);
            }
        }
    }
    for (auto &[job, process] : c.processes) {
        for (const auto &p : c.packages) {
            const bool used = p.native.count(job) ||
                              std::any_of(p.modules.begin(), p.modules.end(),
                                          [&](const auto &module) {
                                              return module.process == job;
                                          }) ||
                              (job == "kppmainapp" &&
                               (!p.ui.empty() || p.ui_runtime || !p.apps.empty())) ||
                              (job == "statusbar" &&
                               (!p.statusbar.empty() ||
                                std::find(p.needs.begin(), p.needs.end(),
                                          "org.kppmods.statusbar") != p.needs.end()));
            if (used) {
                process.packages.push_back(p.id);
            }
        }
    }
    return c;
}
} // namespace kppmods
