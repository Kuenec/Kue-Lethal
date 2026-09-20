#include "platform/Environment.h"

#include "core/Utf8.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

namespace kue::platform {

namespace {

struct FontCandidates {
    std::array<std::string_view, 2> paths;
};

constexpr FontCandidates kBodyFontCandidates{
    {"/usr/share/fonts/TTF/DejaVuSans.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"}};
constexpr FontCandidates kTitleFontCandidates{
    {"/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
     "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"}};

}

EnvironmentValue readEnvironment(const char* name, EnvironmentStorage& storage) noexcept {
    const char* const value = std::getenv(name);
    if (!value)
        return {EnvironmentStatus::Unset, {}};
    if (value[0] == '\0')
        return {EnvironmentStatus::Empty, {}};
    const std::size_t size = ::strnlen(value, kMaximumEnvironmentValueBytes + 1);
    if (size > kMaximumEnvironmentValueBytes)
        return {EnvironmentStatus::TooLong, {}};
    if (!isValidUtf8({value, size}))
        return {EnvironmentStatus::InvalidEncoding, {}};
    std::memcpy(storage.data(), value, size);
    storage[size] = '\0';
    return {EnvironmentStatus::Valid, {storage.data(), size}};
}

int writeEnvironment(const char* name, const char* utf8Value) noexcept {
    errno = 0;
    const int result = utf8Value ? ::setenv(name, utf8Value, 1) : ::unsetenv(name);
    if (result == 0)
        return 0;
    return errno != 0 ? errno : EIO;
}

SystemFontPath systemFontPath(SystemFontRole role, EnvironmentStorage& storage) noexcept {
    const FontCandidates& candidates =
        role == SystemFontRole::Body ? kBodyFontCandidates : kTitleFontCandidates;
    for (const std::string_view candidate : candidates.paths) {
        struct stat metadata{};
        if (::stat(candidate.data(), &metadata) != 0 || !S_ISREG(metadata.st_mode))
            continue;
        std::memcpy(storage.data(), candidate.data(), candidate.size());
        storage[candidate.size()] = '\0';
        return {true, {storage.data(), candidate.size()}};
    }
    return {false, {}};
}

}
