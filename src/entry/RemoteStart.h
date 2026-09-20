#ifndef KUE_ENTRY_REMOTE_START_H
#define KUE_ENTRY_REMOTE_START_H

#include <cstddef>
#include <cstdint>

namespace kue::entry {

inline constexpr std::size_t kRemoteStartPathCapacity = 4096;

struct RemoteStartRequest {
    std::uint32_t configPathBytes;
    std::uint32_t logPathBytes;
    char configPath[kRemoteStartPathCapacity + 1];
    char logPath[kRemoteStartPathCapacity + 1];
};

enum class RemoteStartFailure : int {
    InvalidRequest = 6,
    EnvironmentCaptureFailed = 7,
    EnvironmentMutationFailed = 8,
};

inline constexpr int kRemoteStartResultMask = 0xff;
inline constexpr int kRemoteStartRollbackFailedFlag = 0x100;

}

#endif
