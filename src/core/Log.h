#ifndef KUE_CORE_LOG_H
#define KUE_CORE_LOG_H

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kue {

enum class LogLevel : std::uint8_t { Info, Warning, Error };

inline constexpr std::size_t kMaximumLogPathBytes = 4096;

bool logInit(std::string_view path);
[[gnu::format(printf, 2, 3)]] void logFormat(LogLevel level, const char* format, ...);

#define KUE_INFO(...)                                                                              \
    do {                                                                                           \
        kue::logFormat(kue::LogLevel::Info, __VA_ARGS__);                                          \
    } while (0)

#define KUE_WARN(...)                                                                              \
    do {                                                                                           \
        kue::logFormat(kue::LogLevel::Warning, __VA_ARGS__);                                       \
    } while (0)

#define KUE_ERR(...)                                                                               \
    do {                                                                                           \
        kue::logFormat(kue::LogLevel::Error, __VA_ARGS__);                                         \
    } while (0)

}

#endif
