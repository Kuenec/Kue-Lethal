#ifndef KUE_PLATFORM_FILE_SYSTEM_H
#define KUE_PLATFORM_FILE_SYSTEM_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

namespace kue::platform {

inline constexpr std::size_t kMaximumPathBytes = 4096;
inline constexpr std::size_t kTemporaryNameSuffixBytes = 6;
inline constexpr std::size_t kMaximumTemporaryPathBytes =
    kMaximumPathBytes + sizeof(".tmp.") - 1 + kTemporaryNameSuffixBytes;

struct DescriptorResult {
    int descriptor = -1;
    int errorCode = 0;
};

enum class FileKind : std::uint8_t { Regular, Other };

struct FileInspection {
    bool succeeded = false;
    int errorCode = 0;
    FileKind kind = FileKind::Other;
    std::uint64_t bytes = 0;
};

struct InheritanceInspection {
    bool succeeded = false;
    int errorCode = 0;
    bool inheritable = true;
};

struct ReadResult {
    std::size_t bytes = 0;
    int errorCode = 0;
};

struct StreamResult {
    std::FILE* stream = nullptr;
    int errorCode = 0;
};

enum class ReplacementStatus : std::uint8_t {
    NotCommitted,
    CommittedDurabilityUnconfirmed,
    CommittedCleanupFailed,
    Durable
};

enum class ReplacementOperation : std::uint8_t {
    None,
    Rename,
    OpenDirectory,
    SynchronizeDirectory,
    CloseDirectory
};

struct ReplacementFailure {
    ReplacementOperation operation = ReplacementOperation::None;
    int errorCode = 0;
};

struct ReplacementResult {
    ReplacementStatus status = ReplacementStatus::NotCommitted;
    ReplacementFailure primary;
    ReplacementFailure secondary;
};

struct ConsoleInspection {
    bool succeeded = false;
    int errorCode = 0;
    bool ansi = false;
};

[[nodiscard]] DescriptorResult openForAppend(const char* utf8Path) noexcept;
[[nodiscard]] DescriptorResult openForRead(const char* utf8Path) noexcept;
[[nodiscard]] DescriptorResult createExclusiveTemporary(char* utf8PathTemplate) noexcept;
[[nodiscard]] FileInspection inspectFile(int descriptor) noexcept;
[[nodiscard]] InheritanceInspection inspectInheritance(int descriptor) noexcept;
[[nodiscard]] ReadResult readSome(int descriptor, std::span<char> destination) noexcept;
[[nodiscard]] int closeDescriptor(int descriptor) noexcept;
[[nodiscard]] StreamResult associateStream(int descriptor, const char* mode) noexcept;
[[nodiscard]] int synchronizeDescriptor(int descriptor) noexcept;
[[nodiscard]] int removeFile(const char* utf8Path) noexcept;
[[nodiscard]] ReplacementResult replaceFile(const char* utf8TemporaryPath,
                                            const char* utf8DestinationPath) noexcept;
[[nodiscard]] ConsoleInspection inspectConsole(std::FILE* stream) noexcept;

}

#endif
