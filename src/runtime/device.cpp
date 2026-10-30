// Read device properties from firmware files and exported symbols.
#include "device.hpp"
#include "identifiers.hpp"
#include "process.hpp"
#include "runtime-files.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <syslog.h>

namespace kppmods {
// Match exported/imported symbol spellings instead of assuming an ABI from firmware
// numbers.
std::string kppMainAppExecutable() {
    // Follow the firmware-selected executable when both V1 and V2 are installed.
    for (const char *path : {"/app/bin/KPPMainApp", "/app/bin/KPPMainAppV2"}) {
        if (std::filesystem::exists(path)) {
            return std::filesystem::canonical(path).string();
        }
    }
    return {};
}

std::optional<std::string> detectStringAbi(const AbiSelector &selector) {
    const auto provider =
        selector.provider == "kppmainapp" ? kppMainAppExecutable() : selector.provider;
    if (selector.optional.value_or(false) &&
        (provider.empty() || !std::filesystem::exists(provider))) {
        syslog(LOG_INFO, "kppmods: skipping optional ABI provider %s: absent",
               provider.c_str());
        return std::nullopt;
    }
    if (provider.empty()) {
        throw std::runtime_error("KPPMainApp is not installed in this firmware");
    }
    auto data = readFile(provider);
    bool hasOldAbi = data.find(selector.old_symbol) != std::string::npos,
         hasNewAbi = data.find(selector.new_symbol) != std::string::npos;
    if (!hasOldAbi && !hasNewAbi) {
        if (selector.optional.value_or(false)) {
            syslog(LOG_INFO, "kppmods: skipping optional ABI provider %s: symbols absent",
                   provider.c_str());
            return std::nullopt;
        }
        throw std::runtime_error("C++ ABI symbols not found in provider: " + provider);
    }
    if (hasOldAbi && hasNewAbi) {
        throw std::runtime_error("Ambiguous C++ ABI provider: " + provider);
    }
    return hasNewAbi ? "1" : "0";
}

std::string readFirmware(const std::string &installationRoot) {
    std::string firmware;
    if (std::filesystem::exists(installationRoot + "/device.json")) {
        firmware = km::get<std::string>(readJson(installationRoot + "/device.json"),
                                        "firmware");
    }
    if (std::filesystem::exists("/etc/prettyversion.txt")) {
        firmware.clear();
        auto text = readFile("/etc/prettyversion.txt");
        for (size_t i = 0; i < text.size(); ++i) {
            if (isdigit(static_cast<unsigned char>(text[i]))) {
                size_t end = i;
                while (end < text.size() &&
                       (isdigit(static_cast<unsigned char>(text[end])) ||
                        (text[end] == '.' && end + 1 < text.size() &&
                         isdigit(static_cast<unsigned char>(text[end + 1]))))) {
                    ++end;
                }
                const auto version = text.substr(i, end - i);
                if (std::count(version.begin(), version.end(), '.') >= 2) {
                    firmware = version;
                    break;
                }
                i = end - 1;
            }
        }
    }
    return firmware;
}
} // namespace kppmods
