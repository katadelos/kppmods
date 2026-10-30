#include <jni.h>
#include <dlfcn.h>
#include <cstring>
#include <mutex>
#include <exception>
#include <syslog.h>
#include <vector>
#include "java-classes.hpp"

namespace {
using DefineClass = jclass (*)(JNIEnv *, const char *, jobject, const jbyte *, jsize,
                              jobject, const char *);
struct Helpers {
    jobject loader;
    jclass patches;
    jmethodID transform;
};
std::mutex helperMutex;
std::vector<Helpers> helpers;

bool target(const char *name) {
    if (!name) return false;
    const char *classes[] = {
        "com/amazon/kindle/settings/dialog/DeviceInfoDialog",
        "com/amazon/kindle/settings/dialog/DeviceNameDialog",
        "com/amazon/kindle/settings/dialog/DeviceNameDialog$a",
        "com/amazon/kindle/settings/view/personalize/PersonalizeDisplayController",
        "com/amazon/kindle/settings/view/personalize/DeviceNameAndPersonalInfoDisplayController",
        "com/amazon/kindle/settings/view/settings/SettingsDisplayController",
        "com/amazon/kindle/settings/view/settings/WirelessAndBluetoothDisplayController"
    };
    for (const char *candidate : classes) {
        if (!std::strcmp(name, candidate)) return true;
    }
    return false;
}

Helpers loadHelpers(JNIEnv *env, jobject loader) {
    std::lock_guard<std::mutex> lock(helperMutex);
    for (const auto &entry : helpers) {
        if (env->IsSameObject(entry.loader, loader)) return entry;
    }
    // Keep a failed loader entry too: partially defined helpers must not be
    // defined a second time when another Settings class is loaded.
    helpers.push_back({env->NewGlobalRef(loader), nullptr, nullptr});
    const size_t index = helpers.size() - 1;
    for (const auto &source : antidoxClasses) {
        jclass cls = env->DefineClass(source.name, loader,
                                     reinterpret_cast<const jbyte *>(source.data),
                                     static_cast<jsize>(source.length));
        if (!cls) return {};
        if (!std::strcmp(source.name, "org/kppmods/antidox/Patches")) {
            helpers[index].patches = static_cast<jclass>(env->NewGlobalRef(cls));
        }
        env->DeleteLocalRef(cls);
    }
    if (!helpers[index].patches) return {};
    helpers[index].transform = env->GetStaticMethodID(
        helpers[index].patches, "transform", "([B)[B");
    return helpers[index];
}
} // namespace

// Java Settings uses ClassLoader.defineClass1, whose libjava implementation calls
// this exported JVM entry point. The normal KPP Mods framework preload reaches
// it without changing JVM options or the firmware's jars.
extern "C" __attribute__((visibility("default"))) jclass
JVM_DefineClassWithSource(JNIEnv *env, const char *name, jobject loader,
                         const jbyte *bytes, jsize length, jobject domain,
                         const char *source) {
    static auto original = reinterpret_cast<DefineClass>(
        dlsym(RTLD_NEXT, "JVM_DefineClassWithSource"));
    if (!original) return nullptr;
    if (!target(name) || env->ExceptionCheck()) {
        return original(env, name, loader, bytes, length, domain, source);
    }
    // Exceptions caused by our helpers must not prevent the stock Settings class
    // from loading. Never clear an exception returned by the original JVM call.
    const auto unchanged = [&]() {
        if (env->ExceptionCheck()) env->ExceptionClear();
        syslog(LOG_ERR, "kppmods-antidox: Java display adaptation unavailable for %s", name);
        return original(env, name, loader, bytes, length, domain, source);
    };
    try {
        const Helpers api = loadHelpers(env, loader);
        if (!api.patches || !api.transform) return unchanged();
        jbyteArray input = env->NewByteArray(length);
        if (!input) return unchanged();
        env->SetByteArrayRegion(input, 0, length, bytes);
        auto output = static_cast<jbyteArray>(
            env->CallStaticObjectMethod(api.patches, api.transform, input));
        env->DeleteLocalRef(input);
        if (env->ExceptionCheck() || !output) return unchanged();
        const jsize size = env->GetArrayLength(output);
        std::vector<jbyte> transformed(static_cast<size_t>(size));
        env->GetByteArrayRegion(output, 0, size, transformed.data());
        env->DeleteLocalRef(output);
        if (env->ExceptionCheck()) return unchanged();
        jclass result = original(env, name, loader, transformed.data(), size, domain, source);
        if (result) syslog(LOG_INFO, "kppmods-antidox: adapted Java Settings display %s", name);
        return result;
    } catch (const std::exception &) {
        return unchanged();
    }
}
