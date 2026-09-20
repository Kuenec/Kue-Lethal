#include "main.h"

#include "platform/Environment.h"

#include <cstring>
#include <stdexcept>

extern "C" void kueFixtureCompleteConstructor();
extern "C" void kueFixtureRecordBoot();
extern "C" [[noreturn]] void kueFixtureThrowMaximumException();
extern "C" [[noreturn]] void kueFixtureThrowOversizedException();
extern "C" [[noreturn]] void kueFixtureThrowInvalidException();
namespace {

__attribute__((constructor)) void completeModuleInitialization() {
    kueFixtureCompleteConstructor();
}

}

namespace kue {

BootResult boot() {
    kueFixtureRecordBoot();
    platform::EnvironmentStorage storage;
    const platform::EnvironmentValue requested =
        platform::readEnvironment("KUE_FIXTURE_BOOT", storage);
    if (requested.status != platform::EnvironmentStatus::Valid)
        return BootResult::Running;
    const char* behavior = storage.data();
    if (std::strcmp(behavior, "configuration-failure") == 0)
        return BootResult::ConfigurationFailed;
    if (std::strcmp(behavior, "logging-failure") == 0)
        return BootResult::LoggingFailed;
    if (std::strcmp(behavior, "state-start-failure") == 0)
        return BootResult::StateStartFailed;
    if (std::strcmp(behavior, "standard-exception") == 0)
        throw std::runtime_error("fixture standard failure");
    if (std::strcmp(behavior, "unknown-exception") == 0)
        throw 7;
    if (std::strcmp(behavior, "maximum-exception") == 0)
        kueFixtureThrowMaximumException();
    if (std::strcmp(behavior, "oversized-exception") == 0)
        kueFixtureThrowOversizedException();
    if (std::strcmp(behavior, "invalid-exception") == 0)
        kueFixtureThrowInvalidException();
    return BootResult::Running;
}

}
