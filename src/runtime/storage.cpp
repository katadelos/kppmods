// Persistence and LIPC delivery used by the management broker.
#include "broker.hpp"
#include "runtime-cache.hpp"
namespace kppmods {
namespace runtime {
void saveJsonAtomically(const std::string &path, const Json &value) {
    const auto contents = boost::json::serialize(value);
    auto mirror = mirrorRuntimeState(path, contents);
    km::files::writeFileAtomically(path, contents, 0600);
}

} // namespace runtime
} // namespace kppmods
