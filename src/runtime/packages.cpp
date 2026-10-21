// Validate package input once; dependency resolution operates on typed records.
#include "packages.hpp"
#include "identifiers.hpp"
#include "runtime-files.hpp"
#include <algorithm>
#include <filesystem>
#include <functional>
#include <sys/statvfs.h>

namespace kppmods {
std::string packagePath(const std::string &base, const std::string &rel) {
    if (rel.empty() || rel[0] == '/' || rel.find('\0') != std::string::npos) {
        throw std::runtime_error("Invalid relative path: " + rel);
    }
    std::stringstream ss(rel);
    std::string part;
    while (std::getline(ss, part, '/')) {
        if (part == ".." || part.empty()) {
            throw std::runtime_error("Invalid relative path: " + rel);
        }
    }
    return base + "/" + rel;
}

Package loadPackage(const std::string &directory) {
    Package package =
        boost::json::value_to<Package>(readJson(directory + "/manifest.json"));
    package.root =
        std::filesystem::canonical(directory + "/manifest.json").parent_path().string();
    if (package.schema_version != 2 || !km::isPackageId(package.id)) {
        throw std::runtime_error("Invalid package manifest " + directory);
    }
    for (const auto &[path, message] : package.requiresExecutables) {
        if (!std::filesystem::path(path).is_absolute() ||
            path.find('\0') != std::string::npos || message.empty()) {
            throw std::runtime_error("Invalid executable requirement in " + package.id);
        }
    }
    const auto requireFile = [&](const std::string &path) {
        if (!std::filesystem::exists(packagePath(directory, path))) {
            throw std::runtime_error("Missing package file: " + path);
        }
    };
    if (package.ui.size() > 1) {
        throw std::runtime_error("An extension supports at most one UI entry");
    }
    if (package.ui_runtime) {
        requireFile(package.ui_runtime->host);
        requireFile(package.ui_runtime->toolkit);
    }
    for (const auto &path : package.ui) {
        requireFile(path);
    }
    for (const auto &module : package.modules) {
        if (!km::isLocalIdentifier(module.id) ||
            !km::isLocalIdentifier(module.process)) {
            throw std::runtime_error("Invalid native module");
        }
        requireFile(module.path);
    }
    for (const auto &[job, libraries] : package.native) {
        if (!km::isLocalIdentifier(job)) {
            throw std::runtime_error("Invalid native process: " + job);
        }
        for (const auto &library : libraries) {
            if (const auto *path = std::get_if<std::string>(&library)) {
                requireFile(*path);
            } else {
                const auto &selector = std::get<NativeVariants>(library);
                if (selector.variants.empty() || selector.abi.provider.empty() ||
                    selector.abi.old_symbol.empty() ||
                    selector.abi.new_symbol.empty()) {
                    throw std::runtime_error("Invalid native ABI selector");
                }
                for (const auto &[abi, path] : selector.variants) {
                    requireFile(path);
                }
            }
        }
    }
    return package;
}
std::map<std::string, Package> installedPackages() {
    std::map<std::string, Package> packages;
    std::error_code error;
    for (const auto &entry :
         std::filesystem::directory_iterator(runtimeRoot + "/extensions", error)) {
        const auto id = entry.path().filename().string();
        if (!std::filesystem::exists(runtimeRoot + "/extensions/" + id +
                                     "/manifest.json")) {
            continue;
        }
        auto package = loadPackage(runtimeRoot + "/extensions/" + id);
        if (package.id != id) {
            throw std::runtime_error("Extension directory mismatch: " + id);
        }
        packages.emplace(id, std::move(package));
    }
    return packages;
}
std::map<std::string, std::string>
unavailablePackages(const std::map<std::string, Package> &installed) {
    std::map<std::string, std::string> unavailable;
    for (const auto &[id, package] : installed) {
        for (const auto &[path, message] : package.requiresExecutables) {
            std::error_code error;
            const auto status = std::filesystem::status(path, error);
            constexpr auto executable = std::filesystem::perms::owner_exec |
                                        std::filesystem::perms::group_exec |
                                        std::filesystem::perms::others_exec;
            struct statvfs mount {};
            // This broker runs as root. FSP forwards access(X_OK) to its
            // noexec backing store even when the visible mount allows execve.
            if (error || !std::filesystem::is_regular_file(status) ||
                (status.permissions() & executable) == std::filesystem::perms::none ||
                statvfs(path.c_str(), &mount) || (mount.f_flag & ST_NOEXEC)) {
                unavailable.emplace(id, message);
                break;
            }
        }
    }
    // An unavailable provider also prevents its dependent extensions loading.
    bool changed;
    do {
        changed = false;
        for (const auto &[id, package] : installed) {
            if (unavailable.count(id)) {
                continue;
            }
            for (const auto &dependency : package.needs) {
                if (!installed.count(dependency) || unavailable.count(dependency)) {
                    unavailable.emplace(id, "A required extension is unavailable: " +
                                                dependency);
                    changed = true;
                    break;
                }
            }
        }
    } while (changed);
    return unavailable;
}

void setPackageEnabled(const std::map<std::string, Package> &installed,
                       Disabled &disabled, const std::string &id, bool enabled) {
    const auto unavailable = unavailablePackages(installed);
    std::set<std::string> visited;
    std::function<void(const std::string &)> change = [&](const std::string &current) {
        if (!installed.count(current)) {
            throw std::runtime_error("Extension is not installed: " + current);
        }
        if (!visited.insert(current).second) {
            return;
        }
        if (enabled) {
            if (unavailable.count(current)) {
                throw std::runtime_error(unavailable.at(current));
            }
            disabled.erase(current);
            for (const auto &dependency : installed.at(current).needs) {
                change(dependency);
            }
        } else {
            disabled.insert(current);
            for (const auto &[dependent, package] : installed) {
                if (std::find(package.needs.begin(), package.needs.end(), current) !=
                    package.needs.end()) {
                    change(dependent);
                }
            }
        }
    };
    change(id);
}
std::vector<Package> enabledPackages(const std::map<std::string, Package> &installed,
                                     const Disabled &disabled) {
    for (const auto &id : disabled) {
        if (id == "ui-host" || id == "mod-manager") {
            throw std::runtime_error("Core extension cannot be disabled: " + id);
        }
    }

    const auto unavailable = unavailablePackages(installed);
    std::vector<Package> ordered;
    std::map<std::string, int> marks;
    std::function<void(const std::string &)> visit = [&](const std::string &id) {
        if (disabled.count(id) || !installed.count(id)) {
            throw std::runtime_error("Required extension is unavailable or disabled: " +
                                     id);
        }
        if (marks[id] == 2) {
            return;
        }
        if (marks[id] == 1) {
            throw std::runtime_error("Extension load cycle: " + id);
        }
        marks[id] = 1;
        for (const auto &dependency : installed.at(id).needs) {
            visit(dependency);
        }
        marks[id] = 2;
        ordered.push_back(installed.at(id));
    };
    for (const auto &[id, package] : installed) {
        if (!disabled.count(id) && !unavailable.count(id)) {
            visit(id);
        }
    }
    return ordered;
}
} // namespace kppmods
