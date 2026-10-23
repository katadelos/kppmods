#pragma once
#include "filesystem.hpp"
#include <string>

namespace kppmods {
inline const std::string cacheRoot = "/var/local/kppmods";
inline const std::string cacheState = cacheRoot + "/state";
inline const std::string sourceStore = "/mnt/us/.eink-sdk";
inline const std::string sourceState = sourceStore + "/var/lib/kppmods";
bool prepareRuntimeCache();
void importRuntimeState();
km::files::FileDescriptor mirrorRuntimeState(const std::string &path,
                                             const std::string &contents);
} // namespace kppmods
