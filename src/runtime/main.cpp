#include "broker.hpp"
#include "common/lipc.hpp"
#include "hermes/discovery.hpp"
#include "userstore.hpp"
#include <fcntl.h>
#include <iostream>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <syslog.h>
#include <thread>
#include <unistd.h>

using namespace kppmods::runtime;
namespace {
int client(std::vector<std::string> args) {
    boost::json::object body;
    if (args.empty() || args[0] == "--help") {
        std::cout << "kppmod list | status | configuration | enable ID | disable ID | "
                     "apply DISABLED_JSON | reload [ID] | scan BUNDLE | apps | "
                     "launch-app SCRIPT | development-start | development-stop\n"
                     "kppmod --request OPERATION < BODY_JSON | --config CONFIG_JSON\n";
        return 0;
    }
    if (args.size() > 2) {
        throw std::runtime_error("Too many arguments; see --help");
    }
    if (args.size() == 2 && args[0] == "scan") {
        std::cout << boost::json::serialize(km::hermes::discover(args[1])) << '\n';
        return 0;
    }
    const bool raw = args[0] == "--request";
    std::string operation = "management";
    if (raw) {
        operation = args.at(1);
        const std::string bytes{std::istreambuf_iterator<char>(std::cin), {}};
        if (!bytes.empty()) {
            body = km::parseJson(bytes).as_object();
        }
    } else {
        body["command"] = args[0];
        if (args.size() == 2) {
            body["argument"] = args[1];
        }
    }
    auto result = km::ipc::request(operation, body);
    if (!raw && km::contains(result, "operation")) {
        const auto id = result.at("operation");
        do {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            result =
                km::ipc::request("managementResult", boost::json::object{{"id", id}});
        } while (!km::contains(result, "error") && !km::get<bool>(result, "done"));
        if (!km::contains(result, "error")) {
            result = result.at("value");
        }
    }
    if (!raw && km::contains(result, "error")) {
        throw std::runtime_error(km::get<std::string>(result, "error"));
    }
    std::cout << (!raw && result.is_string() ? std::string(result.as_string())
                                             : boost::json::serialize(result))
              << '\n';
    return km::contains(result, "error") ? 1 : 0;
}
LIPCcode request(LIPC *connection, const char *, LIPCha *input, LIPCha **output,
                 void *data) {
    auto &broker = *static_cast<Broker *>(data);
    std::lock_guard<std::mutex> lock(broker.mutex);
    Json result;
    std::string operation;
    try {
        const auto message = km::parseJson(km::ipc::get(input));
        operation = km::get<std::string>(message, "operation");
        result = broker.handleRequest(operation, km::field(message, "body"));
    } catch (const std::exception &error) {
        syslog(LOG_ERR, "request failed operation=%s: %s", operation.c_str(),
               error.what());
        result = errorResponse(error.what());
    }
    try {
        *output = km::ipc::make(connection, result).release();
    } catch (...) {
        return LIPC_ERROR_INTERNAL;
    }
    return LIPC_OK;
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc == 2 && (std::string(argv[1]).starts_with("--userstore-") ||
                         std::string(argv[1]) == "--prepare-runtime" ||
                         std::string(argv[1]) == "--prepare-export" ||
                         std::string(argv[1]) == "--watch-userstore")) {
            return kppmods::userstoreCommand(argv[1]);
        }
        if (argc != 3 || std::string(argv[1]) != "--config") {
            return client({argv + 1, argv + argc});
        }
        openlog("kppmod", LOG_PID, LOG_USER);
        sigset_t signals;
        sigemptyset(&signals);
        sigaddset(&signals, SIGTERM);
        sigaddset(&signals, SIGINT);
        pthread_sigmask(SIG_BLOCK, &signals, nullptr);
        signal(SIGPIPE, SIG_IGN);
        umask(022);
        Broker broker(argv[2]);
        km::files::FileDescriptor lock(
            open((kppmods::runRoot + "/runtime.lock").c_str(),
                 O_CREAT | O_RDWR | O_CLOEXEC, 0600));
        if (lock.get() < 0 || flock(lock.get(), LOCK_EX | LOCK_NB)) {
            throw std::runtime_error("Runtime already running");
        }
        auto connection = km::ipc::connect(km::ipc::service);
        broker.notify = [&] {
            km::ipc::api().LipcCreateAndSendEvent(connection.get(), "changed");
        };
        km::ipc::check(km::ipc::api().LipcRegisterDynamicHasharrayProperty(
            connection.get(), "request", request, &broker));
        broker.notify();
        std::cerr << "kppmod ready pid=" << getpid() << "\n";
        const timespec interval{0, 250000000};
        while (sigtimedwait(&signals, nullptr, &interval) < 0) {
            if (errno != EAGAIN && errno != EINTR) {
                throw std::runtime_error("Signal wait failed");
            }
            std::lock_guard<std::mutex> guard(broker.mutex);
            broker.retryStatusReset();
            broker.reapCommands();
        }
        {
            std::lock_guard<std::mutex> guard(broker.mutex);
            broker.notify = {};
        }
        if (!broker.managementResults.empty()) {
            broker.managementResults.rbegin()->second.wait();
        }
        connection.reset();
        broker.commands.clear();
        try {
            sendStatusUpdates({"clear_dynamic"});
        } catch (const std::exception &e) {
            std::cerr << "Status cleanup: " << e.what() << '\n';
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "kppmod: " << e.what() << '\n';
        return 1;
    }
}
