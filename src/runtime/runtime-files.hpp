#pragma once
#include "filesystem.hpp"
#include <filesystem>
namespace kppmods {
using km::Json;
inline const std::string runtimeRoot = "/var/local/kppmods/current/lib/kppmods";
inline const std::string runRoot = "/var/run/kppmods";
using km::files::readFile;
using km::files::writeFileAtomically;
inline Json readJson(const std::string &path) {
    return km::parseJson(readFile(path));
}
template <class T> T readJson(const std::string &path, T otherwise) {
    return std::filesystem::exists(path) ? boost::json::value_to<T>(readJson(path))
                                         : std::move(otherwise);
}
} // namespace kppmods
