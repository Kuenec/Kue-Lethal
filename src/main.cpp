#include "main.h"

#include "core/Config.h"
#include "core/Log.h"
#include "game/LethalState.h"
#include "overlay/InternalHud.h"
#include "platform/Environment.h"
#include "platform/Process.h"

#include <mutex>
#include <string_view>
#include <utility>

namespace kue {

namespace {

std::mutex gBootMutex;

static_assert(platform::kMaximumEnvironmentValueBytes == kMaximumLogPathBytes);

}

BootResult boot() {
    std::lock_guard<std::mutex> bootLock(gBootMutex);
    static Config config;
    static LethalState source;
    const LethalWorkerState state = source.workerState();
    if (state == LethalWorkerState::Running)
        return BootResult::Running;
    if (state != LethalWorkerState::Ready)
        return BootResult::StateStartFailed;

    Config candidate;
    std::string configError;
    const std::string requestedConfigPath;
    if (!configLoad(candidate, {.path = requestedConfigPath, .error = configError})) {
        KUE_ERR("configuration load failed: %s", configError.c_str());
        return BootResult::ConfigurationFailed;
    }
    platform::EnvironmentStorage logOverrideStorage;
    const platform::EnvironmentValue logOverride =
        platform::readEnvironment("KUE_LOG", logOverrideStorage);
    std::string_view runtimeLogPath = candidate.logPath;
    switch (logOverride.status) {
    case platform::EnvironmentStatus::Unset:
        break;
    case platform::EnvironmentStatus::Valid:
        runtimeLogPath = logOverride.text;
        break;
    case platform::EnvironmentStatus::Empty:
        KUE_ERR("KUE_LOG is defined but empty");
        return BootResult::LoggingFailed;
    case platform::EnvironmentStatus::TooLong:
        KUE_ERR("KUE_LOG exceeds the 4096-byte limit");
        return BootResult::LoggingFailed;
    case platform::EnvironmentStatus::InvalidEncoding:
        KUE_ERR("KUE_LOG must be valid UTF-8");
        return BootResult::LoggingFailed;
    }
    if (!logInit(runtimeLogPath)) {
        return BootResult::LoggingFailed;
    }

    KUE_INFO("kue-lethal module boot (pid %lu)",
             static_cast<unsigned long>(platform::currentProcessId()));
    KUE_INFO("%s", kBuildIdentity);

    config = std::move(candidate);
    source.apply(config);
    internalhud::initialize(source, config);
    KUE_INFO("kue-lethal: starting internal Unity HUD");
    if (!source.start()) {
        KUE_ERR("source failed to start");
        return BootResult::StateStartFailed;
    }
    return BootResult::Running;
}

}
