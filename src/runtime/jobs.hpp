#pragma once
#include "models.hpp"
namespace kppmods {
std::set<std::string> affectedJobs(const Configuration &previous,
                                   const Configuration &candidate, bool force,
                                   const std::string &reloadPackageId);
std::map<std::string, std::string> jobEnvironment(const Configuration &configuration,
                                                  const std::string &job);
void publishJobEnvironment(const Configuration &configuration);
void stopKppForReload();
void restartJob(const Configuration &configuration, const std::string &job);
void activate(const Configuration &candidate, std::set<std::string> jobs);
} // namespace kppmods
