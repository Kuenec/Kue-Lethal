#include "main.h"

#include "core/Log.h"
#include "core/Utf8.h"

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <exception>
#include <string_view>
#include <unistd.h>

namespace {

enum class StartResult : std::uint8_t { StandardException = 4, UnknownException = 5 };

static_assert(static_cast<int>(kue::BootResult::StateStartFailed) <
              static_cast<int>(StartResult::StandardException));

constexpr std::size_t kMaximumExceptionDetailBytes = 1024;

bool writeAll(int descriptor, std::string_view text) noexcept {
    while (!text.empty()) {
        const ssize_t written = ::write(descriptor, text.data(), text.size());
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

extern "C" [[gnu::visibility("default")]] int kue_start() noexcept {
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
