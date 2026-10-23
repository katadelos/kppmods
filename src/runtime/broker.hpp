#pragma once
#include "child-process.hpp"
#include "configuration.hpp"
#include "diagnostics.hpp"
#include "filesystem.hpp"
#include "identifiers.hpp"
#include "runtime-files.hpp"
#include <ctime>
#include <deque>
#include <filesystem>
#include <functional>
#include <future>
#include <mutex>
#include <set>
#include <sys/types.h>

namespace kppmods {
namespace runtime {
using km::Json;
struct CommandProcess {
    std::unique_ptr<process::Command> child;
    std::string owner, action;
    km::files::FileDescriptor userstore;
};

// The runtime owns activation, package settings, commands and the event stream.
struct Broker {
    Configuration config;
    std::map<std::string, std::set<std::string>> statusItems;
    std::deque<Json> events;
    std::string data, streamId, configPath;
    int eventSequence = 0;
    unsigned commandSequence = 0;
    std::map<std::string, CommandProcess> commands;
    Disabled disabled;
    std::set<std::string> pendingJobs;
    unsigned managementId = 0;
    bool statusReset = false;
    time_t nextStatusReset = 0;

    std::mutex mutex;
    std::function<void()> notify;
    // Destroy futures first: their workers may still use the state above.
    std::map<unsigned, std::shared_future<Json>> managementResults;

    explicit Broker(const std::string &path);
    void resetStatusItems();
    void retryStatusReset();
    void event(const std::string &owner, const std::string &name, Json value);
    void loadConfiguration();
    const Package &package(const std::string &id) const;
    bool managing() const;
    Json manage(const Json &body);
    Json launchAction(const Json &request);
    Json launchCommand(const CommandRequest &input, const std::string &action = "",
                       const std::string &payload = "");
    Json commandResult(const std::string &id, bool cancel);
    void reapCommands();
    // The request callback and maintenance loop hold mutex across complete operations.
    Json handleRequest(const std::string &operation, const Json &body);

  private:
    Json updateSettings(const Json &body);
    Json updateStatusItems(const Json &body);
    std::string developmentDirectory() const;
    Json readDevelopmentUpdate(const Json &body);
    Json saveDevelopmentResult(const Json &body);
};
using km::files::readFile;
void saveJsonAtomically(const std::string &path, const Json &value);
Json errorResponse(const std::string &message);
void sendStatusUpdates(const std::vector<std::string> &updates);
} // namespace runtime
} // namespace kppmods
