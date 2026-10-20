#include "common/json.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <exception>
#include <fstream>
#include <kppmods/kppmods.h>
#include <map>
#include <memory>
#include <mutex>
#include <pwd.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>
#include <vector>

namespace {
struct Module {
    std::string id, root;
    kppmod_context_v2 context{};
    void *library = nullptr;
};
struct Claim {
    Module *owner;
    const void *target;
};
struct Script {
    std::string id, path;
};
struct Host {
    std::recursive_mutex mutex;
    std::vector<std::unique_ptr<Module>> modules;
    std::map<std::string, Claim> claims;
    std::vector<Script> scripts;
    std::string process, config, status, bootstrap;
    bool initialized = false, enabled = false;
};
Host &host() {
    static Host *value = new Host;
    return *value;
}
std::string sanitizeLogField(std::string text) {
    for (char &c : text) {
        if (c == '\t' || c == '\r' || c == '\n') {
            c = ' ';
        }
    }
    return text;
}
void report(const std::string &id, int priority, const std::string &message) {
    Host &state = host();
    syslog(priority, "kppmods[%ld] %s/%s: %s", static_cast<long>(getpid()),
           state.process.c_str(), id.c_str(), message.c_str());
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (!state.status.empty()) {
        FILE *f = fopen(state.status.c_str(), "a");
        if (f) {
            // KPP may drop from root to framework after preload constructors.
            // Keep its per-process diagnostics writable after that transition.
            if (geteuid() == 0) {
                if (passwd *account = getpwnam("framework")) {
                    if (fchown(fileno(f), account->pw_uid, account->pw_gid) != 0) {
                        syslog(LOG_WARNING,
                               "kppmods: cannot change diagnostics ownership: %s",
                               strerror(errno));
                    }
                }
            }
            fprintf(f, "%d\t%s\t%s\n", priority, sanitizeLogField(id).c_str(),
                    sanitizeLogField(message).c_str());
            fclose(f);
        }
    }
}
Module *owner(const kppmod_context_v2 *context) {
    if (!context) {
        return nullptr;
    }
    for (auto &module : host().modules) {
        if (&module->context == context) {
            return module.get();
        }
    }
    return nullptr;
}
void logMessage(const kppmod_context_v2 *context, int priority, const char *text) {
    if (context && text) {
        report(context->id, priority, text);
    }
}
int claimHook(const kppmod_context_v2 *context, const char *name, const void *target) {
    Host &state = host();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    Module *module = owner(context);
    if (!module || !name || !*name) {
        return EINVAL;
    }
    for (const auto &claim : state.claims) {
        if (claim.first == name || (target && claim.second.target == target)) {
            report(module->id, LOG_ERR,
                   std::string("hook conflict ") + name + " owned by " +
                       claim.second.owner->id);
            return EBUSY;
        }
    }
    state.claims.emplace(name, Claim{module, target});
    return 0;
}
const kppmod_api_v2 api = {KPPMOD_ABI_V2, sizeof(kppmod_api_v2), logMessage, claimHook};
// Upstart shells inherit LD_PRELOAD too. Only initialize inside the intended
// executable.
bool processMatches(const std::string &process) {
    char path[1024];
    ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (n < 0) {
        return false;
    }
    path[n] = '\0';
    const char *expected = getenv("KPPMOD_EXECUTABLE");
    if (expected && *expected) {
        return strcmp(path, expected) == 0;
    }
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (process == "kppmainapp") {
        return strcmp(base, "KPPMainAppV2") == 0 || strcmp(base, "KPPMainApp") == 0;
    }
    if (process == "framework") {
        return strcmp(base, "cvm") == 0 || strcmp(base, "java") == 0;
    }
    if (process == "webreader") {
        return strcmp(base, "webreader") == 0;
    }
    if (process == "statusbar") {
        return strcmp(base, "JunoStatusBarDriver") == 0;
    }
    return base == process;
}
void removeRegistrations(Module *module) {
    Host &state = host();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    for (auto it = state.claims.begin(); it != state.claims.end();) {
        if (it->second.owner == module) {
            it = state.claims.erase(it);
        } else {
            ++it;
        }
    }
}
void loadModule(const km::Json &fields) {
    Host &state = host();
    if (km::get<std::string>(fields, "id") == "kppmods.host") {
        report(km::get<std::string>(fields, "id"), LOG_ERR,
               "reserved module identifier");
        return;
    }
    for (const auto &module : state.modules) {
        if (module->id == km::get<std::string>(fields, "id")) {
            report(km::get<std::string>(fields, "id"), LOG_ERR,
                   "duplicate module identifier");
            return;
        }
    }
    std::unique_ptr<Module> pointer(new Module);
    Module *module = pointer.get();
    module->id = km::get<std::string>(fields, "id");
    module->root = km::get<std::string>(fields, "root");
    module->context = {KPPMOD_ABI_V2,
                       sizeof(kppmod_context_v2),
                       module->id.c_str(),
                       module->root.c_str(),
                       state.process.c_str(),
                       state.config.c_str(),
                       nullptr};
    state.modules.push_back(std::move(pointer));
    module->library =
        dlopen(km::get<std::string>(fields, "path").c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!module->library) {
        report(module->id, LOG_ERR, dlerror());
        return;
    }
    auto init =
        reinterpret_cast<kppmod_init_fn_v2>(dlsym(module->library, "kppmod_init_v2"));
    if (!init) {
        report(module->id, LOG_ERR, "missing kppmod_init_v2");
        return;
    }
    int result = EFAULT;
    try {
        result = init(&api, &module->context);
    } catch (const std::exception &error) {
        report(module->id, LOG_ERR, error.what());
    } catch (...) {
        report(module->id, LOG_ERR, "initialization threw an exception");
    }
    if (result) {
        removeRegistrations(module);
        report(module->id, LOG_ERR, "initialization failed: " + std::to_string(result));
    } else {
        report(module->id, LOG_INFO, "active " + km::get<std::string>(fields, "path"));
    }
}
void initialize() {
    Host &state = host();
    std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if (state.initialized) {
        return;
    }
    state.initialized = true;
    const char *process = getenv("KPPMOD_PROCESS"), *config = getenv("KPPMOD_CONFIG");
    if (!process || !config || !processMatches(process)) {
        return;
    }
    state.process = process;
    state.config = config;
    if (state.process.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") !=
        std::string::npos) {
        return;
    }
    const char *statusDir = getenv("KPPMOD_STATUS_DIR");
    if (statusDir && *statusDir) {
        state.status = std::string(statusDir) + "/" + state.process + "-" +
                       std::to_string(getpid()) + ".status";
    }
    std::ifstream input(state.config);
    if (!input) {
        report("host", LOG_ERR, "cannot read configuration " + state.config);
        return;
    }
    state.enabled = true;
    // Reserve the injection hook before any module can acquire it.
    std::unique_ptr<Module> self(new Module);
    self->id = "kppmods.host";
    state.claims.emplace("reactNative.Bridge.loadBundle", Claim{self.get(), nullptr});
    state.claims.emplace("reactNative.NativeModule.invoke", Claim{self.get(), nullptr});
    state.claims.emplace("reactNative.NativeModuleInterface.shutdown",
                         Claim{self.get(), nullptr});
    state.modules.push_back(std::move(self));
    report("host", LOG_INFO, "configuration " + state.config);
    auto configuration =
        km::parseJson(std::string(std::istreambuf_iterator<char>(input), {}));
    const auto &runtime = km::field(configuration, "processes", state.process);
    for (const auto &script : km::arrayItems(runtime, "scripts")) {
        state.scripts.push_back(
            {km::get<std::string>(script, "id"), km::get<std::string>(script, "path")});
    }
    if (!state.scripts.empty()) {
        state.bootstrap =
            "KPPMods.start(" + boost::json::serialize(configuration) + ");";
        state.scripts.push_back({"kppmods.bootstrap", ""});
    }
    for (const auto &plugin : km::arrayItems(runtime, "plugins")) {
        loadModule(plugin);
    }
}
using Load = bool (*)(void *, const std::string &);
using Eval = bool (*)(void *, std::string &&);
#if _GLIBCXX_USE_CXX11_ABI
#define LOAD_SYMBOL                                                                    \
    "_ZN11reactNative6Bridge10loadBundleERKNSt7__cxx1112basic_stringIcSt11char_"       \
    "traitsIcESaIcEEE"
#define EVAL_SYMBOL                                                                    \
    "_ZN11reactNative6Bridge20loadScriptFromStringEONSt7__cxx1112basic_"               \
    "stringIcSt11char_traitsIcESaIcEEE"
#else
#define LOAD_SYMBOL "_ZN11reactNative6Bridge10loadBundleERKSs"
#define EVAL_SYMBOL "_ZN11reactNative6Bridge20loadScriptFromStringEOSs"
#endif
} // namespace
extern "C" bool kppmods_transport_enabled() {
    initialize();
    return host().enabled && host().process == "kppmainapp";
}
extern "C" __attribute__((visibility("default"))) bool
kppmods_load_bundle(void *, const std::string &) asm(LOAD_SYMBOL);
extern "C" bool kppmods_load_bundle(void *bridge, const std::string &path) {
    static Load real = reinterpret_cast<Load>(dlsym(RTLD_NEXT, LOAD_SYMBOL));
    if (!real) {
        report("host", LOG_ERR, "original loadBundle unavailable");
        return false;
    }
    const bool result = real(bridge, path);
    initialize();
    Host &state = host();
    if (!result || !state.enabled || state.process != "kppmainapp" ||
        (path != "/app/KPPMainApp/js/KPPMainApp.js" &&
         path != "/app/KPPMainApp/js/KPPMainApp.js.hbc")) {
        return result;
    }
    auto eval = reinterpret_cast<Eval>(dlsym(RTLD_NEXT, EVAL_SYMBOL));
    if (!eval) {
        report("host", LOG_ERR, "loadScriptFromString unavailable");
        return result;
    }
    for (const auto &script : state.scripts) {
        try {
            std::ifstream input(script.path, std::ios::binary);
            if (!script.path.empty() && !input) {
                report(script.id, LOG_ERR, "cannot read script " + script.path);
                continue;
            }
            std::string js =
                script.path.empty()
                    ? state.bootstrap
                    : std::string(std::istreambuf_iterator<char>(input), {});
            if ((!script.path.empty() && input.bad()) || js.empty() ||
                js.size() > 8 * 1024 * 1024) {
                report(script.id, LOG_ERR, "invalid script size or read failure");
                continue;
            }
            const bool ok = eval(bridge, std::move(js));
            report(script.id, ok ? LOG_INFO : LOG_ERR,
                   ok ? "script evaluated" : "script evaluation failed");
        } catch (const std::exception &error) {
            report(script.id, LOG_ERR, error.what());
        } catch (...) {
            report(script.id, LOG_ERR, "script evaluation threw an exception");
        }
    }
    return result;
}
__attribute__((constructor)) static void kppmods_start() {
    try {
        initialize();
    } catch (...) {
        syslog(LOG_ERR, "kppmods: host initialization failed");
    }
}
