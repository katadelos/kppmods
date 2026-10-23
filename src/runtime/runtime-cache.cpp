#include "runtime-cache.hpp"
#include "filesystem.hpp"
#include "userstore.hpp"
#include <algorithm>
#include <array>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace kppmods {
namespace fs = std::filesystem;
namespace {
km::files::FileDescriptor stateLock() {
    km::files::FileDescriptor lock(
        open((cacheRoot + "/state.lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600));
    if (lock.get() < 0 || flock(lock.get(), LOCK_EX)) {
        throw std::runtime_error("Cannot lock runtime settings");
    }
    return lock;
}
bool sameFile(const fs::path &a, const fs::path &b) {
    std::error_code error;
    if (!fs::is_regular_file(b, error) || fs::file_size(a) != fs::file_size(b)) {
        return false;
    }
    std::ifstream left(a, std::ios::binary), right(b, std::ios::binary);
    std::array<char, 16384> first{}, second{};
    while (left) {
        left.read(first.data(), first.size());
        right.read(second.data(), second.size());
        if (left.gcount() != right.gcount() ||
            !std::equal(first.begin(), first.begin() + left.gcount(), second.begin())) {
            return false;
        }
    }
    if (left.bad() || right.bad()) {
        throw std::runtime_error("Could not compare runtime cache files");
    }
    return true;
}
void collect(std::map<std::string, fs::path> &files, const fs::path &source,
             const std::string &target) {
    if (!fs::is_directory(source)) {
        throw std::runtime_error("Missing runtime directory: " + source.string());
    }
    for (const auto &entry : fs::recursive_directory_iterator(source)) {
        if (entry.is_directory()) {
            continue;
        }
        if (!entry.is_regular_file()) {
            throw std::runtime_error("Unsupported runtime file: " +
                                     entry.path().string());
        }
        files[target + "/" + entry.path().lexically_relative(source).string()] =
            fs::canonical(entry.path());
    }
}
void collectOldCaches(const fs::path &current) {
    std::string mappings;
    for (const auto &process : fs::directory_iterator("/proc")) {
        std::ifstream input(process.path() / "maps");
        mappings.append(std::istreambuf_iterator<char>(input), {});
    }
    for (const auto &entry : fs::directory_iterator(cacheRoot + "/runtimes")) {
        if (entry.path() != current &&
            mappings.find(entry.path().string() + "/") == std::string::npos) {
            fs::remove_all(entry.path());
        }
    }
}
} // namespace

bool prepareRuntimeCache() {
    if (geteuid() || !userstoreMounted()) {
        throw std::runtime_error("Runtime caching requires root and mounted userstore");
    }
    fs::create_directories(cacheRoot + "/runtimes");
    km::files::FileDescriptor lock(
        open((cacheRoot + "/cache.lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600));
    if (lock.get() < 0 || flock(lock.get(), LOCK_EX)) {
        throw std::runtime_error("Cannot lock runtime cache");
    }
    std::map<std::string, fs::path> files;
    files["bin/kppmod"] = sourceStore + "/usr/bin/kppmod";
    collect(files, sourceStore + "/usr/lib/kppmods", "lib/kppmods");
    collect(files, sourceStore + "/usr/share/kppmods", "share/kppmods");
    files["lib/libstdc++.so.6"] = fs::canonical("/var/run/eink-sdk/lib/libstdc++.so.6");
    // libgcc is part of this device's firmware runtime; copy it so the cache has
    // an explicit dependency set alongside the SDK's C++ library.
    files["lib/libgcc_s.so.1"] = fs::canonical("/lib/libgcc_s.so.1");
    std::uintmax_t bytes = 0;
    std::string manifest;
    bool changed = false;
    for (const auto &[name, source] : files) {
        bytes += fs::file_size(source);
        manifest += name + "\n";
        changed = changed || !sameFile(source, cacheRoot + "/current/" + name);
    }
    const auto manifestPath = cacheRoot + "/current/files";
    changed = changed || !fs::exists(manifestPath) ||
              km::files::readFile(manifestPath) != manifest;
    if (!changed) {
        collectOldCaches(fs::canonical(cacheRoot + "/current"));
        return false;
    }
    if (fs::space(cacheRoot).available < bytes + 16 * 1024 * 1024) {
        throw std::runtime_error(
            "Runtime cache needs its payload size plus 16 MiB free in /var/local");
    }
    std::string staging = cacheRoot + "/runtimes/runtime.XXXXXX";
    if (!mkdtemp(staging.data())) {
        throw std::runtime_error("Cannot create runtime cache staging directory");
    }
    chmod(staging.c_str(), 0755);
    try {
        for (const auto &[name, source] : files) {
            const auto target = fs::path(staging) / name;
            fs::create_directories(target.parent_path());
            fs::copy_file(source, target);
            fs::permissions(target, fs::status(source).permissions());
        }
        km::files::writeFileAtomically(staging + "/files", manifest);
        sync();
        const auto temporary = cacheRoot + "/current.new";
        fs::remove(temporary);
        fs::create_symlink(staging, temporary);
        fs::rename(temporary, cacheRoot + "/current");
        sync();
    } catch (...) {
        fs::remove_all(staging);
        throw;
    }
    collectOldCaches(staging);
    return true;
}

void importRuntimeState() {
    if (!userstoreMounted()) {
        throw std::runtime_error("Userstore is not mounted");
    }
    auto lock = stateLock();
    fs::create_directories(cacheState);
    fs::create_directories(sourceState);
    // config.json is generated against the selected runtime, never imported.
    std::set<std::string> wanted;
    for (const auto &entry : fs::recursive_directory_iterator(sourceState)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto relative = entry.path().lexically_relative(sourceState).string();
        if (relative == "config.json") {
            continue;
        }
        wanted.insert(relative);
        const auto destination = fs::path(cacheState) / relative;
        fs::create_directories(destination.parent_path());
        if (!sameFile(entry.path(), destination)) {
            km::files::writeFileAtomically(destination,
                                           km::files::readFile(entry.path()), 0600);
        }
    }
    for (const auto &entry : fs::recursive_directory_iterator(cacheState)) {
        const auto relative = entry.path().lexically_relative(cacheState).string();
        if (entry.is_regular_file() && relative != "config.json" &&
            !wanted.count(relative)) {
            fs::remove(entry.path());
        }
    }
}

km::files::FileDescriptor mirrorRuntimeState(const std::string &path,
                                             const std::string &contents) {
    const auto prefix = cacheState + "/";
    if (path.rfind(prefix, 0) != 0) {
        return {};
    }
    // Callers hold an admission lease until both copies are durable. An
    // already-admitted operation may finish while new work is being blocked.
    if (!userstoreMounted()) {
        throw std::runtime_error(
            "Cannot save mod settings while userstore is unavailable");
    }
    auto lock = stateLock();
    const auto destination = sourceState + path.substr(cacheState.size());
    fs::create_directories(fs::path(destination).parent_path());
    km::files::writeFileAtomically(destination, contents, 0600);
    return lock;
}
} // namespace kppmods
