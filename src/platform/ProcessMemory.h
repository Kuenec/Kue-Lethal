#ifndef KUE_PLATFORM_PROCESS_MEMORY_H
#define KUE_PLATFORM_PROCESS_MEMORY_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#if !defined(_WIN32)
#include <sys/mman.h>
#endif

namespace kue::platform {

#if defined(_WIN32)
using PageProtection = unsigned long;
inline constexpr PageProtection kPageReadWrite = 0x04;
inline constexpr PageProtection kPageReadExecute = 0x20;
inline constexpr PageProtection kPageReadWriteExecute = 0x40;
#else
using PageProtection = int;
inline constexpr PageProtection kPageReadWrite = PROT_READ | PROT_WRITE;
inline constexpr PageProtection kPageReadExecute = PROT_READ | PROT_EXEC;
inline constexpr PageProtection kPageReadWriteExecute = PROT_READ | PROT_WRITE | PROT_EXEC;
#endif

enum class MemoryRegionKind : std::uint8_t { Private, Image, Mapped };

struct MemoryRegion {
    std::uintptr_t begin = 0;
    std::uintptr_t end = 0;
    PageProtection protection = 0;
    MemoryRegionKind kind = MemoryRegionKind::Private;
    bool readable = false;
    bool executable = false;
    std::uintptr_t fileOffset = 0;
    std::string_view path;
};

inline constexpr std::size_t kMaximumMemoryMapLineBytes = 4352;

class MemoryRegionScan final {
  public:
    MemoryRegionScan() noexcept;
    ~MemoryRegionScan();
    MemoryRegionScan(const MemoryRegionScan&) = delete;
    MemoryRegionScan& operator=(const MemoryRegionScan&) = delete;

    [[nodiscard]] bool next(MemoryRegion& region) noexcept;

  private:
#if defined(_WIN32)
    std::uintptr_t mCursor = 0;
    std::uintptr_t mLimit = 0;
#else
    [[nodiscard]] bool readLine(std::size_t& lineBytes) noexcept;

    int mDescriptor = -1;
    std::array<char, 8192> mBuffer{};
    std::size_t mBufferBytes = 0;
    std::size_t mBufferPosition = 0;
    std::array<char, kMaximumMemoryMapLineBytes> mLine{};
    bool mExhausted = false;
#endif
};

[[nodiscard]] std::size_t pageSize() noexcept;
[[nodiscard]] void* reserveNear(std::uintptr_t address, std::size_t bytes) noexcept;
void releaseReservation(void* address, std::size_t bytes) noexcept;
[[nodiscard]] bool setProtection(void* address, std::size_t bytes,
                                 PageProtection protection) noexcept;
void flushInstructionCache(void* address, std::size_t bytes) noexcept;

}

#endif
