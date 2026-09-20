#ifndef KUE_TESTS_ALIGNED_ALLOCATION_H
#define KUE_TESTS_ALIGNED_ALLOCATION_H

#include <cstddef>
#include <cstdlib>

#if defined(_WIN32)
#include <malloc.h>
#endif

namespace kue::tests {

inline void* allocateAligned(std::size_t size, std::size_t alignment) noexcept {
    const std::size_t actualSize = size == 0 ? 1 : size;
#if defined(_WIN32)
    return ::_aligned_malloc(actualSize, alignment);
#else
    void* memory = nullptr;
    return ::posix_memalign(&memory, alignment, actualSize) == 0 ? memory : nullptr;
#endif
}

inline void releaseAligned(void* memory) noexcept {
#if defined(_WIN32)
    ::_aligned_free(memory);
#else
    std::free(memory);
#endif
}

}

#endif
