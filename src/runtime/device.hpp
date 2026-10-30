#pragma once
#include "models.hpp"
namespace kppmods {
using km::Json;
std::string readFirmware(const std::string &installationRoot);
std::string kppMainAppExecutable();
std::string detectHostStringAbi();
std::optional<std::string> detectStringAbi(const AbiSelector &selector);
} // namespace kppmods
