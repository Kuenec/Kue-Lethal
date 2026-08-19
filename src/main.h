#ifndef KUE_MAIN_H
#define KUE_MAIN_H

#include <cstdint>

namespace kue {

extern const char kBuildIdentity[];

enum class BootResult : std::uint8_t {
    Running = 0,
    ConfigurationFailed = 1,
    LoggingFailed = 2,
    StateStartFailed = 3,
};

[[nodiscard]] BootResult boot();

}

#endif
