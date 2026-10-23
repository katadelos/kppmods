// LIPC operations for settings, extension events and status-bar items.
#include "broker.hpp"
#include "common/lipc.hpp"
#include "userstore.hpp"
#include <ctime>
#include <unistd.h>

namespace kppmods {
namespace runtime {
Broker::Broker(const std::string &path) : configPath(path) {
    timespec started;
    if (clock_gettime(CLOCK_MONOTONIC, &started)) {
        throw std::runtime_error("Cannot identify event stream");
    }
    streamId = std::to_string(getpid()) + "-" + std::to_string(started.tv_sec) + "-" +
               std::to_string(started.tv_nsec);
    loadConfiguration();
}
void Broker::event(const std::string &owner, const std::string &name, Json value) {
    boost::json::object e;
    e["seq"] = ++eventSequence;
    e["package"] = owner;
    e["name"] = name;
    e["data"] = value;
    events.push_back(e);
    if (notify) {
        notify();
    }
    if (events.size() > 256) {
        events.pop_front();
    }
}

Json Broker::handleRequest(const std::string &operation, const Json &body) {
    km::files::FileDescriptor admission;
    if (operation == "settings" || operation == "development" ||
        operation == "developmentResult" || operation == "action") {
        admission = userstoreAdmission();
    }
    if (operation == "management") {
        return manage(body);
    }
    if (operation == "managementResult") {
        const auto found = managementResults.find(km::get<unsigned>(body, "id"));
        if (found == managementResults.end()) {
            throw std::runtime_error("Management result unavailable");
        }
        return found->second.wait_for(std::chrono::seconds(0)) !=
                       std::future_status::ready
                   ? Json(boost::json::object{{"done", false}})
                   : Json(boost::json::object{{"done", true},
                                              {"value", found->second.get()}});
    }

    if (operation == "exec") {
        return launchCommand(boost::json::value_to<CommandRequest>(body));
    }
    if (operation == "command") {
        return commandResult(km::get<std::string>(body, "id"),
                             km::get<bool>(body, "cancel"));
    }
    if (operation == "development") {
        return readDevelopmentUpdate(body);
    }
    if (operation == "developmentResult") {
        return saveDevelopmentResult(body);
    }
    if (operation == "health") {
        boost::json::object j;
        j["ready"] = true;
        j["pid"] = int(getpid());
        return j;
    }
    if (operation == "state") {
        const int since = km::get<int>(body, "since");
        boost::json::object result;
        result["settings"] = config.settings;
        result["seq"] = eventSequence;
        result["streamId"] = streamId;
        result["events"] = boost::json::array{};
        for (const auto &e : events) {
            if (km::get<int>(e, "seq") > since) {
                result["events"].as_array().push_back(Json(e));
            }
        }
        return result;
    }
    if (operation == "action") {
        return launchAction(body);
    }
    if (operation == "status") {
        return updateStatusItems(body);
    }
    if (operation == "settings") {
        return updateSettings(body);
    }
    if (operation == "events") {
        std::string id = km::get<std::string>(body, "package"),
                    name = km::get<std::string>(body, "name");
        if (!config.settings.contains(id) || name.empty() || name.size() > 128) {
            throw std::runtime_error("Invalid event");
        }
        event(id, name, km::field(body, "data"));
        boost::json::object result;
        result["seq"] = eventSequence;
        return result;
    }
    throw std::runtime_error("Unknown operation");
}

Json errorResponse(const std::string &message) {
    boost::json::object e;
    e["error"] = message;
    return e;
}

} // namespace runtime
} // namespace kppmods
