#ifndef KUE_PLATFORM_ENVIRONMENT_H
#define KUE_PLATFORM_ENVIRONMENT_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kue::platform {

inline constexpr std::size_t kMaximumEnvironmentValueBytes = 4096;

using EnvironmentStorage = std::array<char, kMaximumEnvironmentValueBytes + 1>;

enum class EnvironmentStatus : std::uint8_t { Unset, Empty, Valid, TooLong, InvalidEncoding };

struct EnvironmentValue {
    EnvironmentStatus status = EnvironmentStatus::Unset;
    std::string_view text;
};

[[nodiscard]] EnvironmentValue readEnvironment(const char* name,
                                               EnvironmentStorage& storage) noexcept;
[[nodiscard]] int writeEnvironment(const char* name, const char* utf8Value) noexcept;

struct UserConfigurationLocation {
    const char* variableName;
    std::string_view suffix;
};

enum class SystemFontRole : std::uint8_t { Body, Title };

struct SystemFontPath {
    bool available = false;
    std::string_view path;
};

[[nodiscard]] SystemFontPath systemFontPath(SystemFontRole role,
                                            EnvironmentStorage& storage) noexcept;

#if defined(_WIN32)
inline constexpr char kPathSeparator = '\\';
inline constexpr UserConfigurationLocation kUserConfigurationLocation{"APPDATA",
                                                                      "\\kuelethal\\config.json"};
#else
inline constexpr char kPathSeparator = '/';
inline constexpr UserConfigurationLocation kUserConfigurationLocation{
    "HOME", "/.config/kuelethal/config.json"};
#endif

}

#endif
