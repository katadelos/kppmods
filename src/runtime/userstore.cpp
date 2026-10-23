#include "userstore.hpp"
#include "child-process.hpp"
#include "process.hpp"
#include "runtime-cache.hpp"
#include "runtime-files.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <sys/file.h>
#include <sys/stat.h>
#include <syslog.h>
#include <thread>
#include <unistd.h>

namespace kppmods {
namespace fs = std::filesystem;
namespace {
const std::string admissionPath = "/var/run/eink-sdk-userstore.lock";
std::string readOptional(const fs::path &path) {
    std::ifstream input(path);
    std::string value{std::istreambuf_iterator<char>(input), {}};
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
        value.pop_back();
    }
    return value;
}
std::string link(const fs::path &path) {
    std::error_code error;
    return fs::read_symlink(path, error).string();
}
km::files::FileDescriptor admissionLock() {
    km::files::FileDescriptor lock(
        open(admissionPath.c_str(),
             (geteuid() ? O_RDONLY : O_CREAT | O_RDWR) | O_CLOEXEC, 0644));
    if (lock.get() < 0) {
        throw std::runtime_error("Cannot open SDK userstore lock");
    }
    return lock;
}
void state(const std::string &value) {
    fs::create_directories(runRoot);
    km::files::writeFileAtomically(storageState, value);
    syslog(LOG_INFO, "userstore %s", value.c_str());
}
bool updateRunning(bool inspectLock = true) {
    if (inspectLock) {
        km::files::FileDescriptor packageLock(
            open("/tmp/eink-opkg.lock", O_RDONLY | O_CLOEXEC));
        if (packageLock.get() >= 0) {
            struct flock lock{};
            lock.l_type = F_WRLCK;
            lock.l_whence = SEEK_SET;
            if (fcntl(packageLock.get(), F_GETLK, &lock) || lock.l_type != F_UNLCK) {
                return true;
            }
        }
    }
    for (const auto &entry : fs::directory_iterator("/proc")) {
        const auto executable = link(entry.path() / "exe");
        if (fs::path(executable).filename() == "opkg" ||
            (executable == sourceStore + "/usr/bin/kppmod" &&
             readOptional(entry.path() / "cmdline").find("--prepare-runtime") !=
                 std::string::npos)) {
            return true;
        }
    }
    return false;
}
bool sdkPath(const std::string &value) {
    for (const auto &root : {sourceStore, std::string("/var/run/eink-sdk"),
                             std::string("/mnt/base-us/.eink-sdk")}) {
        if (value == root || value.find(root + "/") != std::string::npos) {
            return true;
        }
    }
    return false;
}
std::string boundedCommand(const std::vector<std::string> &args, int seconds,
                           bool allowFailure = false) {
    process::Command command(args, "/", "root", {}, seconds);
    while (!command.poll()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (*command.result != 0 && !allowFailure) {
        throw std::runtime_error("Userstore command failed: " + args.front() + ": " +
                                 command.err.text);
    }
    return command.out.text;
}
std::set<int> sdkProcesses() {
    std::set<int> result;
    for (const auto &entry : fs::directory_iterator("/proc")) {
        const auto name = entry.path().filename().string();
        if (name.empty() || !std::all_of(name.begin(), name.end(), ::isdigit)) {
            continue;
        }
        const auto pid = std::stoi(name);
        if (pid == getpid()) {
            continue;
        }
        const auto executable = link(entry.path() / "exe");
        const auto environment = readOptional(entry.path() / "environ");
        const auto mappings = readOptional(entry.path() / "maps");
        const std::string marker = std::string("\0", 1) + "EINK_SDK_SESSION=1" + '\0';
        const bool managed = sdkPath(executable) ||
                             (std::string("\0", 1) + environment + '\0').find(marker) !=
                                 std::string::npos;
        bool references = (managed && executable != "/dev/supernet-sdk/bin/supernet") ||
                          sdkPath(mappings) || executable.starts_with("/mnt/us/") ||
                          mappings.find(" /mnt/us/") != std::string::npos ||
                          mappings.find(" /mnt/base-us/") != std::string::npos ||
                          sdkPath(link(entry.path() / "cwd"));
        std::error_code error;
        for (fs::directory_iterator fd(entry.path() / "fd", error), end;
             !error && fd != end; fd.increment(error)) {
            references = references || sdkPath(link(fd->path()));
        }
        if (!references) {
            continue;
        }
        // Cached injected stock processes must never retain SDK mappings. Do
        // not kill a firmware process to hide an incomplete runtime migration.
        if (!managed && executable.rfind("/dev/supernet-sdk/", 0) != 0) {
            throw std::runtime_error("Process " + name +
                                     " still references the SDK: " + executable);
        }
        result.insert(pid);
    }
    return result;
}
void stopJobs() {
    // Only package-owned jobs, never copied firmware job definitions.
    const auto directory = fs::path(cacheRoot + "/current/share/kppmods/upstart");
    if (!fs::is_directory(directory)) {
        return;
    }
    std::set<std::string> jobs{"kppmods-app"};
    for (const auto &entry : fs::directory_iterator(directory)) {
        if (entry.path().extension() == ".conf") {
            jobs.insert(entry.path().stem().string());
        }
    }
    std::string stopped = readOptional(runRoot + "/resume-jobs");
    if (!stopped.empty()) {
        stopped += '\n';
    }
    for (const auto &job : jobs) {
        if (job == "kppmods" || job == "kppmods-storage") {
            continue;
        }
        if (boundedCommand({"/sbin/initctl", "status", job}, 3, true)
                .find("start/running") == std::string::npos) {
            continue;
        }
        // App windows close for USB transfer; services resume after remount.
        if (job != "kppmods-app") {
            stopped += job + "\n";
            km::files::writeFileAtomically(runRoot + "/resume-jobs", stopped);
        }
        boundedCommand({"/sbin/initctl", "stop", job}, 5);
    }
    km::files::writeFileAtomically(runRoot + "/resume-jobs", stopped);
}
void abortExport() {
    const bool mounted = userstoreMounted();
    if (mounted) {
        boundedCommand({"/bin/mount", "-i", "-o", "remount,exec", "/mnt/us"}, 5);
    }
    km::files::writeFileAtomically(runRoot + "/resume-needed", "1");
    state(mounted ? "local" : "blocked");
}
void prepareExport() {
    km::files::FileDescriptor packageLock;
    try {
        // This runs inside volumd's synchronous mntus call. Never use volumd
        // LIPC properties here: volumd holds its state mutex until we return.
        for (unsigned i = 0; updateRunning(); ++i) {
            if (i == 40) {
                throw std::runtime_error("An SDK package update is still running; "
                                         "storage export was deferred");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        // opkg uses a POSIX record lock, not flock. Hold it while closing SDK
        // consumers; a package operation racing this boundary must not write.
        packageLock.reset(
            open("/tmp/eink-opkg.lock", O_CREAT | O_RDWR | O_CLOEXEC, 0600));
        struct flock packageLease{};
        packageLease.l_type = F_WRLCK;
        packageLease.l_whence = SEEK_SET;
        if (packageLock.get() < 0 || fcntl(packageLock.get(), F_SETLK, &packageLease)) {
            throw std::runtime_error("An SDK package operation owns userstore");
        }
        state("preparing");
        fs::remove(runRoot + "/userstore-error");
        stopJobs();
        // Block direct launches of userstore binaries as well as SDK launcher
        // requests. Existing processes can still finish their shutdown; the
        // restore path reapplies the SDK's normal executable mount policy.
        boundedCommand({"/bin/mount", "-i", "-o", "remount,noexec", "/mnt/us"}, 5);
        auto lock = admissionLock();
        std::set<int> signalled;
        bool ready = false;
        for (unsigned i = 0; i < 40; ++i) {
            if (updateRunning(false)) {
                throw std::runtime_error(
                    "An SDK package update started; storage export was cancelled");
            }
            const auto processes = sdkProcesses();
            for (const int pid : processes) {
                if (signalled.insert(pid).second) {
                    kill(pid, SIGTERM);
                }
            }
            if (processes.empty() && !flock(lock.get(), LOCK_EX | LOCK_NB)) {
                ready = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        if (!ready) {
            throw std::runtime_error("An SDK application did not release userstore; "
                                     "storage export was cancelled");
        }
        sync();
        // The wrapper marks exported only after stock mntus has unmounted the
        // backing filesystem. Until then the watcher must not resume anything.
    } catch (const std::exception &error) {
        abortExport();
        km::files::writeFileAtomically(runRoot + "/userstore-error", error.what());
        throw;
    }
}
void resumeUserstore(km::files::FileDescriptor transition) {
    if (!userstoreMounted()) {
        throw std::runtime_error("Userstore has not remounted successfully");
    }
    km::files::writeFileAtomically(runRoot + "/resume-needed", "1");
    state("restoring");
    boundedCommand({"/bin/sh", sourceStore + "/control/activate"}, 30);
    const bool changed = prepareRuntimeCache();
    importRuntimeState();
    // The broker stays alive from its cache. Reconcile only changed extensions;
    // an ordinary eject does not restart the stock UI or change enabled mods.
    state("local");
    auto admission = userstoreAdmission();
    transition.reset();
    // Never hold the mntus transition lock across LIPC or a stock job restart.
    if (runCommand(
            {cacheRoot + "/current/bin/kppmod", changed ? "reload" : "reconcile"})) {
        state("restoring");
        throw std::runtime_error("Could not restore the mod configuration");
    }
    std::istringstream jobs(readOptional(runRoot + "/resume-jobs"));
    std::string job;
    while (std::getline(jobs, job)) {
        if (!job.empty() && boundedCommand({"/sbin/initctl", "status", job}, 3, true)
                                    .find("start/running") == std::string::npos) {
            boundedCommand({"/sbin/initctl", "start", job}, 10);
        }
    }
    fs::remove(runRoot + "/resume-jobs");
    fs::remove(runRoot + "/resume-needed");
}
volatile sig_atomic_t stopping = 0;
void stop(int) {
    stopping = 1;
}
void watchUserstore() {
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    while (!stopping) {
        try {
            const auto phase = readOptional(storageState);
            if (userstoreMounted() && !updateRunning() &&
                (phase == "exported" || phase == "remounting" || phase == "blocked" ||
                 phase == "restoring" || fs::exists(runRoot + "/resume-needed") ||
                 fs::exists(runRoot + "/resume-jobs") ||
                 !fs::exists(cacheRoot + "/current/files"))) {
                km::files::FileDescriptor transition(
                    open((runRoot + "/handover.lock").c_str(),
                         O_CREAT | O_RDWR | O_CLOEXEC, 0600));
                if (transition.get() >= 0 &&
                    !flock(transition.get(), LOCK_EX | LOCK_NB)) {
                    resumeUserstore(std::move(transition));
                }
            }
        } catch (const std::exception &error) {
            syslog(LOG_ERR, "USB handover: %s", error.what());
            km::files::writeFileAtomically(runRoot + "/userstore-error", error.what());
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
} // namespace

bool userstoreMounted() {
    std::ifstream mounts("/proc/mounts");
    std::string source, target, type, options, rest;
    bool mounted = false;
    while (mounts >> source >> target >> type >> options) {
        std::getline(mounts, rest);
        mounted = mounted || target == "/mnt/base-us" ||
                  (target == "/mnt/us" && type != "fuse.fsp" && type != "fsp");
    }
    return mounted && access((sourceStore + "/usr/bin/kppmod").c_str(), R_OK) == 0;
}
void requireUserstore() {
    const auto phase = readOptional(storageState);
    if ((!phase.empty() && phase != "local") || !userstoreMounted()) {
        throw std::runtime_error("USB file transfer is active; eject the Kindle before "
                                 "using SDK applications");
    }
}
km::files::FileDescriptor userstoreAdmission() {
    auto lock = admissionLock();
    if (flock(lock.get(), LOCK_SH | LOCK_NB)) {
        throw std::runtime_error("Userstore handover is in progress");
    }
    requireUserstore();
    return lock;
}
int userstoreCommand(const std::string &command) {
    if (geteuid()) {
        throw std::runtime_error("Userstore management requires root");
    }
    openlog("kppmods-storage", LOG_PID, LOG_USER);
    if (command == "--prepare-runtime") {
        auto admission = userstoreAdmission();
        prepareRuntimeCache();
        importRuntimeState();
    } else if (command == "--prepare-export") {
        prepareExport();
    } else if (command == "--userstore-remounted") {
        state("remounting");
    } else if (command == "--userstore-exported") {
        state("exported");
    } else if (command == "--userstore-abort") {
        abortExport();
    } else if (command == "--watch-userstore") {
        watchUserstore();
    } else {
        throw std::runtime_error("Unknown userstore command");
    }
    return 0;
}
} // namespace kppmods
