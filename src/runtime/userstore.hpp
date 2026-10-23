#pragma once
#include "filesystem.hpp"
#include <string>

namespace kppmods {
inline const std::string storageState = "/var/run/kppmods/userstore";
bool userstoreMounted();
void requireUserstore();
km::files::FileDescriptor userstoreAdmission();
int userstoreCommand(const std::string &command);
} // namespace kppmods
