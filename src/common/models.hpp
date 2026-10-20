#pragma once
#include "json.hpp"
#include <boost/describe.hpp>
#include <optional>
#include <variant>

namespace kppmods {
// These declarations are the structural schema. Semantic checks (paths, users,
// runtime load order) remain with the code that owns those policies.
struct UiRuntime {
    std::string host, toolkit;
    bool operator==(const UiRuntime &) const = default;
};
BOOST_DESCRIBE_STRUCT(UiRuntime, (), (host, toolkit))
struct NativeModule {
    std::string id, process, path;
    bool operator==(const NativeModule &) const = default;
};
BOOST_DESCRIBE_STRUCT(NativeModule, (), (id, process, path))
struct StatusItem {
    std::string id, icon, uri;
    bool operator==(const StatusItem &) const = default;
};
BOOST_DESCRIBE_STRUCT(StatusItem, (), (id, icon, uri))
struct Action {
    std::vector<std::string> argv;
    std::optional<std::string> user;
    std::optional<int> timeout;
    bool operator==(const Action &) const = default;
};
BOOST_DESCRIBE_STRUCT(Action, (), (argv, user, timeout))
struct ActionRequest {
    std::string package, action;
    std::optional<km::Json> args;
};
BOOST_DESCRIBE_STRUCT(ActionRequest, (), (package, action, args))
struct CommandRequest {
    std::string package;
    std::optional<std::string> command, user, cwd;
    std::optional<std::vector<std::string>> argv;
    std::optional<int> timeout;
};
BOOST_DESCRIBE_STRUCT(CommandRequest, (), (package, command, user, cwd, argv, timeout))
// Package/runtime records use their declared defaults for omitted fields. A present
// value must still convert to the declared type; explicit null is only optional.
template <class T> T readRecord(const km::Json &json) {
    const auto &object = json.as_object();
    T result{};
    boost::mp11::mp_for_each<boost::describe::describe_members<
        T, boost::describe::mod_public | boost::describe::mod_inherited>>(
        [&](auto member) {
            if (const auto *value = object.if_contains(member.name)) {
                using Field = std::decay_t<decltype(result.*member.pointer)>;
                result.*member.pointer = boost::json::value_to<Field>(*value);
            }
        });
    return result;
}
struct AbiSelector {
    std::string provider, old_symbol, new_symbol;
    std::optional<bool> optional = std::nullopt;
    bool operator==(const AbiSelector &) const = default;
};
BOOST_DESCRIBE_STRUCT(AbiSelector, (), (provider, old_symbol, new_symbol, optional))
struct NativeVariants {
    AbiSelector abi;
    std::map<std::string, std::string> variants;
    bool operator==(const NativeVariants &) const = default;
};
BOOST_DESCRIBE_STRUCT(NativeVariants, (), (abi, variants))
using NativeLibrary = std::variant<std::string, NativeVariants>;
using Arguments = std::vector<std::string>;
struct Manifest {
    int schema_version = 0;
    std::string id, version, title, description;
    std::vector<std::string> ui, needs;
    std::map<std::string, std::string> requiresExecutables;
    std::optional<UiRuntime> ui_runtime;
    std::vector<NativeModule> modules;
    std::vector<StatusItem> statusbar;
    std::map<std::string, Action> actions;
    std::map<std::string, std::vector<NativeLibrary>> native;
    boost::json::object options, settings, settingsSchema;
    boost::json::array apps;
    bool operator==(const Manifest &) const = default;
};
BOOST_DESCRIBE_STRUCT(Manifest, (),
                      (schema_version, id, version, title, description, ui, needs,
                       requiresExecutables, ui_runtime, modules, statusbar, actions,
                       native, options, settings, settingsSchema, apps))
inline Manifest tag_invoke(boost::json::value_to_tag<Manifest>, const km::Json &json) {
    return readRecord<Manifest>(json);
}
} // namespace kppmods
