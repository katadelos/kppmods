// Asynchronous command execution for trusted in-process extensions.
#include "broker.hpp"
#include "userstore.hpp"
#include <algorithm>
#include <syslog.h>
#include <utf8.h>

namespace kppmods {
namespace runtime {
Json Broker::launchCommand(const CommandRequest &input, const std::string &action,
                           const std::string &payload) {
    auto admission = userstoreAdmission();
    const auto &owner = input.package;
    if (!config.settings.contains(owner)) {
        throw std::runtime_error("Unknown extension");
    }
    size_t running = 0;
    for (const auto &entry : commands) {
        running += !entry.second.child->result;
    }
    if (running >= 8) {
        throw std::runtime_error("Too many running commands");
    }
    while (commands.size() >= 32) {
        auto old =
            std::find_if(commands.begin(), commands.end(),
                         [](const std::pair<const std::string, CommandProcess> &p) {
                             return p.second.child->result.has_value();
                         });
        if (old == commands.end()) {
            break;
        }
        commands.erase(old);
    }
    if (bool(input.command) == bool(input.argv)) {
        throw std::runtime_error("Supply exactly one of command or argv");
    }
    auto args = input.command
                    ? std::vector<std::string>{"/bin/sh", "-c", *input.command}
                    : *input.argv;
    if (args.empty()) {
        throw std::runtime_error("argv must be a nonempty array");
    }
    if (args.size() > 256 || args[0].empty() || args[0][0] != '/') {
        throw std::runtime_error("Use an absolute executable path");
    }
    for (const auto &arg : args) {
        if (arg.size() > 65536 || arg.find('\0') != std::string::npos) {
            throw std::runtime_error("Invalid command argument");
        }
    }
    std::string user = input.user.value_or("framework");
    if (user != "framework" && user != "root") {
        throw std::runtime_error("Unsupported user");
    }
    std::string cwd = input.cwd.value_or(package(owner).root);
    if (cwd.empty() || cwd[0] != '/' || cwd.find('\0') != std::string::npos) {
        throw std::runtime_error("cwd must be an absolute path");
    }
    int timeout = input.timeout.value_or(60);
    if (timeout < 1 || timeout > 3600) {
        throw std::runtime_error("Timeout must be 1–3600 seconds");
    }
    process::Environment environment = {
        {"EINK_SDK_SESSION", "1"},
        {"KPPMOD_PACKAGE", owner},
        {"KPPMOD_PACKAGE_ROOT", package(owner).root},
        {"DISPLAY", getenv("DISPLAY") ? getenv("DISPLAY") : ":0.0"}};
    if (!action.empty()) {
        environment["KPPMOD_ACTION_ARGS"] = payload;
    }
    auto child = std::make_unique<process::Command>(args, cwd, user,
                                                    std::move(environment), timeout);
    const auto pid = child->child.id();
    std::string id = streamId + "-" + std::to_string(++commandSequence);
    commands.emplace(id, CommandProcess{std::move(child), owner, action, std::move(admission)});
    boost::json::object result;
    result["id"] = id;
    result["done"] = false;
    result["pid"] = int(pid);
    if (!action.empty()) {
        result["action"] = action;
        event(owner, "action:start", result);
    }
    return result;
}
void Broker::reapCommands() {
    for (auto &entry : commands) {
        auto &c = entry.second;
        auto &child = *c.child;
        if (child.poll()) {
            c.userstore.reset();
            boost::json::object outcome{{"id", entry.first},
                                        {"exitCode", *child.result},
                                        {"timedOut", child.timedOut},
                                        {"cancelled", child.cancelled}};
            if (*child.result != 0) {
                const auto detail =
                    utf8::replace_invalid(child.err.text.substr(0, 4096));
                outcome["stderr"] = detail;
                syslog(LOG_ERR,
                       "command failed package=%s id=%s exit=%d timeout=%d "
                       "cancelled=%d: %s",
                       c.owner.c_str(), entry.first.c_str(), *child.result,
                       child.timedOut, child.cancelled, detail.c_str());
            }
            event(c.owner, "command:exit", outcome);
            if (!c.action.empty()) {
                event(c.owner, "action:exit",
                      boost::json::object{{"id", entry.first},
                                          {"action", c.action},
                                          {"pid", int(child.child.id())},
                                          {"exitCode", *child.result}});
            }
        }
    }
}
Json Broker::commandResult(const std::string &id, bool cancel) {
    auto it = commands.find(id);
    if (it == commands.end()) {
        throw std::runtime_error("Command result unavailable");
    }
    auto &child = *it->second.child;
    if (cancel) {
        child.cancel();
    }
    reapCommands();
    boost::json::object result;
    result["id"] = id;
    result["done"] = child.result.has_value();
    if (child.result) {
        result["stdout"] = utf8::replace_invalid(child.out.text);
        result["stderr"] = utf8::replace_invalid(child.err.text);
        result["exitCode"] = *child.result;
        result["timedOut"] = child.timedOut;
        result["cancelled"] = child.cancelled;
        result["truncated"] = child.truncated;
    }
    return result;
}
} // namespace runtime
} // namespace kppmods
