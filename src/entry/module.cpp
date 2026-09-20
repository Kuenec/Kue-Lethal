#include "main.h"

#include "core/Log.h"
#include "core/Utf8.h"
#include "entry/RemoteStart.h"
#include "platform/Environment.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <exception>
#include <string_view>
#include <unistd.h>

#if defined(_WIN32)
#define KUE_EXPORT __declspec(dllexport)
#else
#define KUE_EXPORT [[gnu::visibility("default")]]
#endif

namespace {

enum class StartResult : std::uint8_t { StandardException = 4, UnknownException = 5 };

static_assert(static_cast<int>(kue::BootResult::StateStartFailed) <
              static_cast<int>(StartResult::StandardException));
static_assert(static_cast<int>(StartResult::UnknownException) <
              static_cast<int>(kue::entry::RemoteStartFailure::InvalidRequest));
static_assert(static_cast<int>(kue::entry::RemoteStartFailure::EnvironmentMutationFailed) <=
              kue::entry::kRemoteStartResultMask);

constexpr std::size_t kMaximumExceptionDetailBytes = 1024;

constexpr std::size_t kMaximumFallbackWriteBytes = 1024 * 1024;

bool writeAll(int descriptor, std::string_view text) noexcept {
    while (!text.empty()) {
        const auto count =
            static_cast<unsigned int>(std::min(text.size(), kMaximumFallbackWriteBytes));
        const ssize_t written = ::write(descriptor, text.data(), count);
        if (written > 0) {
            text.remove_prefix(static_cast<std::size_t>(written));
            continue;
        }
        if (written < 0 && errno == EINTR)
            continue;
        return false;
    }
    return true;
}

std::string_view boundedExceptionDetail(const char* detail) noexcept {
    const std::size_t size = ::strnlen(detail, kMaximumExceptionDetailBytes + 1);
    if (size > kMaximumExceptionDetailBytes)
        return "<detail exceeds 1024-byte limit>";
    const std::string_view text(detail, size);
    if (!kue::isValidUtf8(text))
        return "<detail is not valid UTF-8>";
    return text;
}

void writeFallback(std::string_view record) noexcept {
    if (!writeAll(STDERR_FILENO, record))
        std::terminate();
}

void writeStandardFallback(std::string_view detail) noexcept {
    constexpr std::string_view prefix = "[kue] loader: start failed with standard exception: ";
    constexpr std::string_view suffix = "; logging failed\n";
    std::array<char, prefix.size() + kMaximumExceptionDetailBytes + suffix.size()> record;
    char* output = record.data();
    std::memcpy(output, prefix.data(), prefix.size());
    output += prefix.size();
    std::memcpy(output, detail.data(), detail.size());
    output += detail.size();
    std::memcpy(output, suffix.data(), suffix.size());
    output += suffix.size();
    writeFallback({record.data(), static_cast<std::size_t>(output - record.data())});
}

void reportStandardException(const std::exception& exception) noexcept {
    const std::string_view detail = boundedExceptionDetail(exception.what());
    try {
        kue::logFormat(kue::LogLevel::Error, "start failed with standard exception: %.*s",
                       static_cast<int>(detail.size()), detail.data());
    } catch (...) {
        writeStandardFallback(detail);
    }
}

void reportUnknownException() noexcept {
    try {
        kue::logFormat(kue::LogLevel::Error, "start failed with unknown exception");
    } catch (...) {
        writeFallback("[kue] loader: start failed with unknown exception; logging failed\n");
    }
}

}

extern "C" KUE_EXPORT int kue_start() noexcept {
    try {
        return static_cast<int>(kue::boot());
    } catch (const std::exception& exception) {
        reportStandardException(exception);
        return static_cast<int>(StartResult::StandardException);
    } catch (...) {
        reportUnknownException();
        return static_cast<int>(StartResult::UnknownException);
    }
}

#if defined(_WIN32)

namespace {

using kue::entry::RemoteStartFailure;
using kue::entry::RemoteStartRequest;

struct PreviousEnvironment {
    kue::platform::EnvironmentStorage configStorage;
    kue::platform::EnvironmentStorage logStorage;
    kue::platform::EnvironmentValue config;
    kue::platform::EnvironmentValue log;
};

bool validRequestPath(const char* text, std::uint32_t bytes) noexcept {
    if (bytes == 0 || bytes > kue::entry::kRemoteStartPathCapacity || text[bytes] != '\0')
        return false;
    const std::string_view path(text, bytes);
    return path.find('\0') == std::string_view::npos && kue::isValidUtf8(path);
}

bool captureRestorable(const kue::platform::EnvironmentValue& value) noexcept {
    return value.status != kue::platform::EnvironmentStatus::TooLong &&
           value.status != kue::platform::EnvironmentStatus::InvalidEncoding;
}

const char* restorationValue(const kue::platform::EnvironmentValue& value,
                             const kue::platform::EnvironmentStorage& storage) noexcept {
    switch (value.status) {
    case kue::platform::EnvironmentStatus::Valid:
        return storage.data();
    case kue::platform::EnvironmentStatus::Empty:
        return "";
    case kue::platform::EnvironmentStatus::Unset:
    case kue::platform::EnvironmentStatus::TooLong:
    case kue::platform::EnvironmentStatus::InvalidEncoding:
        return nullptr;
    }
    return nullptr;
}

bool restoreEnvironment(const PreviousEnvironment& previous) noexcept {
    const int configResult = kue::platform::writeEnvironment(
        "KUE_CONFIG", restorationValue(previous.config, previous.configStorage));
    const int logResult = kue::platform::writeEnvironment(
        "KUE_LOG", restorationValue(previous.log, previous.logStorage));
    return configResult == 0 && logResult == 0;
}

}

extern "C" KUE_EXPORT unsigned long kue_start_remote(void* request) noexcept {
    const auto* const start = static_cast<const RemoteStartRequest*>(request);
    if (!start || !validRequestPath(start->configPath, start->configPathBytes) ||
        !validRequestPath(start->logPath, start->logPathBytes)) {
        return static_cast<unsigned long>(RemoteStartFailure::InvalidRequest);
    }
    PreviousEnvironment previous;
    previous.config = kue::platform::readEnvironment("KUE_CONFIG", previous.configStorage);
    previous.log = kue::platform::readEnvironment("KUE_LOG", previous.logStorage);
    if (!captureRestorable(previous.config) || !captureRestorable(previous.log))
        return static_cast<unsigned long>(RemoteStartFailure::EnvironmentCaptureFailed);
    if (kue::platform::writeEnvironment("KUE_CONFIG", start->configPath) != 0)
        return static_cast<unsigned long>(RemoteStartFailure::EnvironmentMutationFailed);
    if (kue::platform::writeEnvironment("KUE_LOG", start->logPath) != 0) {
        const int result = static_cast<int>(RemoteStartFailure::EnvironmentMutationFailed);
        return static_cast<unsigned long>(
            restoreEnvironment(previous) ? result
                                         : result | kue::entry::kRemoteStartRollbackFailedFlag);
    }
    const int result = kue_start();
    if (result == static_cast<int>(kue::BootResult::Running))
        return static_cast<unsigned long>(result);
    return static_cast<unsigned long>(restoreEnvironment(previous)
                                          ? result
                                          : result | kue::entry::kRemoteStartRollbackFailedFlag);
}

#endif
