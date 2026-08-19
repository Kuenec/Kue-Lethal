#include "main.h"

#include "core/Config.h"
#include "core/Log.h"
#include "core/Utf8.h"
#include "game/LethalState.h"
#include "overlay/InternalHud.h"

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string_view>
#include <unistd.h>
#include <utility>

namespace kue {

namespace {

std::mutex gBootMutex;

std::string_view boundedLogPath(const char* path) noexcept {
    return {path, ::strnlen(path, kMaximumLogPathBytes + 1)};
}

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
    const char* logOverride = ::getenv("KUE_LOG");
    std::string_view runtimeLogPath = candidate.logPath;
    if (logOverride) {
        if (logOverride[0] == '\0') {
            KUE_ERR("KUE_LOG is defined but empty");
            return BootResult::LoggingFailed;
        }
        runtimeLogPath = boundedLogPath(logOverride);
        if (runtimeLogPath.size() > kMaximumLogPathBytes) {
            KUE_ERR("KUE_LOG exceeds the 4096-byte limit");
            return BootResult::LoggingFailed;
        }
        if (!isValidUtf8(runtimeLogPath)) {
            KUE_ERR("KUE_LOG must be valid UTF-8");
            return BootResult::LoggingFailed;
        }
    }
    if (!logInit(runtimeLogPath)) {
        return BootResult::LoggingFailed;
    }

    KUE_INFO("kue-lethal module boot (pid %d)", getpid());
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
