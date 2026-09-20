#include "platform/ProcessMemory.h"

#include "platform/WindowsSupport.h"

#include <bit>

namespace kue::platform {

namespace {

constexpr DWORD kExecutableProtections =
    PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
constexpr DWORD kReadableProtections = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                                       PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                                       PAGE_EXECUTE_WRITECOPY;
constexpr DWORD kProtectionModifiers = PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE;

MemoryRegionKind regionKind(DWORD type) noexcept {
    if (type == MEM_IMAGE)
        return MemoryRegionKind::Image;
    if (type == MEM_MAPPED)
        return MemoryRegionKind::Mapped;
    return MemoryRegionKind::Private;
}

}

MemoryRegionScan::MemoryRegionScan() noexcept {
    SYSTEM_INFO information{};
    ::GetSystemInfo(&information);
    mCursor = std::bit_cast<std::uintptr_t>(information.lpMinimumApplicationAddress);
    mLimit = std::bit_cast<std::uintptr_t>(information.lpMaximumApplicationAddress);
}

MemoryRegionScan::~MemoryRegionScan() = default;

bool MemoryRegionScan::next(MemoryRegion& region) noexcept {
    while (mCursor < mLimit) {
        MEMORY_BASIC_INFORMATION information{};
        if (::VirtualQuery(std::bit_cast<LPCVOID>(mCursor), &information, sizeof(information)) ==
            0) {
            mCursor = mLimit;
            return false;
        }
        const std::uintptr_t begin = std::bit_cast<std::uintptr_t>(information.BaseAddress);
        const std::uintptr_t end = begin + information.RegionSize;
        if (end <= mCursor) {
            mCursor = mLimit;
            return false;
        }
        mCursor = end;
        if (information.State != MEM_COMMIT)
            continue;
        const DWORD protection = information.Protect;
        const bool guarded = (protection & PAGE_GUARD) != 0;
        const DWORD access = protection & ~kProtectionModifiers;
        region = {.begin = begin,
                  .end = end,
                  .protection = protection,
                  .kind = regionKind(information.Type),
                  .readable = !guarded && (access & kReadableProtections) != 0,
                  .executable = !guarded && (access & kExecutableProtections) != 0,
                  .fileOffset = 0,
                  .path = {}};
        return true;
    }
    return false;
}

std::size_t pageSize() noexcept {
    SYSTEM_INFO information{};
    ::GetSystemInfo(&information);
    return information.dwPageSize > 0 ? static_cast<std::size_t>(information.dwPageSize) : 4096;
}

void* reserveNear(std::uintptr_t address, std::size_t bytes) noexcept {
    return ::VirtualAlloc(std::bit_cast<LPVOID>(address), bytes, MEM_RESERVE | MEM_COMMIT,
                          PAGE_READWRITE);
}

void releaseReservation(void* address, std::size_t bytes) noexcept {
    static_cast<void>(bytes);
    static_cast<void>(::VirtualFree(address, 0, MEM_RELEASE));
}

bool setProtection(void* address, std::size_t bytes, PageProtection protection) noexcept {
    DWORD previous = 0;
    return ::VirtualProtect(address, bytes, protection, &previous) != 0;
}

void flushInstructionCache(void* address, std::size_t bytes) noexcept {
    static_cast<void>(::FlushInstructionCache(::GetCurrentProcess(), address, bytes));
}

}
