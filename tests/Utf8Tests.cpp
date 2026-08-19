#include "core/Utf8.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string_view>
#include <type_traits>

namespace {

constexpr std::size_t kManagedCodeUnitCapacity = 512;
constexpr std::size_t kMaximumUtf8Bytes = kManagedCodeUnitCapacity * 3;
constexpr std::size_t kPerformanceSamples = 2000;
constexpr std::size_t kPerformanceBatchSize = 8;
constexpr long long kP95ThresholdNanoseconds = 5000;
constexpr long long kP99ThresholdNanoseconds = 10000;

class TestRun final {
  public:
    void expect(bool condition, std::string_view contract) {
        ++mAssertions;
        if (condition)
            return;
        ++mFailures;
        std::cerr << "FAIL: " << contract << '\n';
    }

    int result() const {
        if (mFailures == 0)
            std::cout << mAssertions << " assertions passed\n";
        return mFailures == 0 ? 0 : 1;
    }

  private:
    int mAssertions = 0;
    int mFailures = 0;
};

std::atomic<bool> gMeasureAllocations{false};
std::atomic<std::size_t> gAllocations{0};
std::atomic<std::size_t> gDeallocations{0};

void recordAllocation() noexcept {
    if (gMeasureAllocations.load(std::memory_order_relaxed))
        gAllocations.fetch_add(1, std::memory_order_relaxed);
}

void recordDeallocation() noexcept {
    if (gMeasureAllocations.load(std::memory_order_relaxed))
        gDeallocations.fetch_add(1, std::memory_order_relaxed);
}

void* allocate(std::size_t size) {
    recordAllocation();
    if (void* memory = std::malloc(size == 0 ? 1 : size))
        return memory;
    throw std::bad_alloc();
}

void* allocateNoThrow(std::size_t size) noexcept {
    recordAllocation();
    return std::malloc(size == 0 ? 1 : size);
}

void* allocateAligned(std::size_t size, std::size_t alignment) {
    recordAllocation();
    void* memory = nullptr;
    if (posix_memalign(&memory, alignment, size == 0 ? 1 : size) == 0)
        return memory;
    throw std::bad_alloc();
}

void* allocateAlignedNoThrow(std::size_t size, std::size_t alignment) noexcept {
    recordAllocation();
    void* memory = nullptr;
    return posix_memalign(&memory, alignment, size == 0 ? 1 : size) == 0 ? memory : nullptr;
}

void release(void* memory) noexcept {
    recordDeallocation();
    std::free(memory);
}

void expectConverted(TestRun& run, kue::Utf16Input input, std::string_view expected) {
    std::array<char, 16> output{};
    output.fill(static_cast<char>(0x5a));
    const kue::Utf16ToUtf8Result result =
        kue::convertUtf16ToUtf8(input, {output.data(), expected.size()});
    run.expect(result.status == kue::Utf16ToUtf8Status::Success,
               "well-formed UTF-16 converts successfully");
    run.expect(result.validatedCodeUnits == input.codeUnitCount,
               "successful conversion validates every input code unit");
    run.expect(result.utf8Bytes == expected.size(),
               "successful conversion reports the exact UTF-8 byte count");
    run.expect(std::string_view(output.data(), result.utf8Bytes) == expected,
               "successful conversion preserves the exact expected bytes");
    run.expect(kue::isValidUtf8({output.data(), result.utf8Bytes}),
               "every converted sequence is well-formed UTF-8");
    run.expect(output[expected.size()] == static_cast<char>(0x5a),
               "conversion writes no byte beyond the reported count");
}

struct ExpectedRejection {
    kue::Utf16ToUtf8Status status;
    std::size_t validatedCodeUnits;
    std::size_t utf8Bytes;
};

void expectRejected(TestRun& run, kue::Utf16Input input, kue::Utf8Output output,
                    ExpectedRejection expected, const std::array<char, 16>& before) {
    const kue::Utf16ToUtf8Result result = kue::convertUtf16ToUtf8(input, output);
    run.expect(result.status == expected.status, "rejected input reports its exact typed cause");
    run.expect(result.validatedCodeUnits == expected.validatedCodeUnits,
               "rejected input reports its exact validated prefix");
    run.expect(result.utf8Bytes == expected.utf8Bytes,
               "rejected input reports the exact UTF-8 prefix or required count");
    if (output.bytes) {
        run.expect(std::equal(before.begin(), before.end(), output.bytes),
                   "every rejected conversion leaves the complete output unchanged");
    }
}

void testDeclaredContract(TestRun& run) {
    static_assert(std::is_same_v<std::underlying_type_t<kue::Utf16ToUtf8Status>, std::uint8_t>);
    static_assert(std::is_trivially_copyable_v<kue::Utf16Input>);
    static_assert(std::is_trivially_copyable_v<kue::Utf8Output>);
    static_assert(std::is_trivially_copyable_v<kue::Utf16ToUtf8Result>);
    static_assert(!std::is_polymorphic_v<kue::Utf16Input>);
    static_assert(!std::is_polymorphic_v<kue::Utf8Output>);
    static_assert(!std::is_polymorphic_v<kue::Utf16ToUtf8Result>);
    static_assert(noexcept(kue::convertUtf16ToUtf8({}, {})));
    static_assert(sizeof(kue::Utf16Input) <= 2 * sizeof(void*));
    static_assert(sizeof(kue::Utf8Output) <= 2 * sizeof(void*));
    static_assert(sizeof(kue::Utf16ToUtf8Result) <= 3 * sizeof(std::size_t));
    run.expect(true, "the converter contract uses only direct bounded values");
}

void testUtf8ValidatorPreserved(TestRun& run) {
    constexpr std::array<char, 10> valid = {static_cast<char>(0x4d), static_cast<char>(0xd0),
                                            static_cast<char>(0xb0), static_cast<char>(0xe4),
                                            static_cast<char>(0xba), static_cast<char>(0x8c),
                                            static_cast<char>(0xf0), static_cast<char>(0x90),
                                            static_cast<char>(0x8c), static_cast<char>(0x82)};
    constexpr std::array<char, 2> overlong = {static_cast<char>(0xc0), static_cast<char>(0xaf)};
    constexpr std::array<char, 2> truncated = {static_cast<char>(0xe2), static_cast<char>(0x82)};
    constexpr std::array<char, 3> surrogate = {static_cast<char>(0xed), static_cast<char>(0xa0),
                                               static_cast<char>(0x80)};
    constexpr std::array<char, 4> beyondMaximum = {static_cast<char>(0xf4), static_cast<char>(0x90),
                                                   static_cast<char>(0x80),
                                                   static_cast<char>(0x80)};
    run.expect(kue::isValidUtf8({}), "the existing validator still accepts empty UTF-8");
    run.expect(kue::isValidUtf8({valid.data(), valid.size()}),
               "the existing validator still accepts all UTF-8 widths");
    run.expect(!kue::isValidUtf8({overlong.data(), overlong.size()}),
               "the existing validator still rejects overlong UTF-8");
    run.expect(!kue::isValidUtf8({truncated.data(), truncated.size()}),
               "the existing validator still rejects truncated UTF-8");
    run.expect(!kue::isValidUtf8({surrogate.data(), surrogate.size()}),
               "the existing validator still rejects UTF-8 surrogate encodings");
    run.expect(!kue::isValidUtf8({beyondMaximum.data(), beyondMaximum.size()}),
               "the existing validator still rejects values beyond U+10FFFF");
}

void testEncodingBoundaries(TestRun& run) {
    constexpr std::array<std::uint16_t, 2> ascii{0x0001, 0x007f};
    constexpr std::array<std::uint16_t, 2> twoByte{0x0080, 0x07ff};
    constexpr std::array<std::uint16_t, 4> threeByte{0x0800, 0xd7ff, 0xe000, 0xffff};
    constexpr std::array<std::uint16_t, 2> firstSupplementary{0xd800, 0xdc00};
    constexpr std::array<std::uint16_t, 2> installedExample{0xd800, 0xdf02};
    constexpr std::array<std::uint16_t, 2> finalSupplementary{0xdbff, 0xdfff};
    expectConverted(run, {ascii.data(), ascii.size()}, "\x01\x7f");
    expectConverted(run, {twoByte.data(), twoByte.size()}, "\xc2\x80\xdf\xbf");
    expectConverted(run, {threeByte.data(), threeByte.size()},
                    "\xe0\xa0\x80\xed\x9f\xbf\xee\x80\x80\xef\xbf\xbf");
    expectConverted(run, {firstSupplementary.data(), firstSupplementary.size()},
                    "\xf0\x90\x80\x80");
    expectConverted(run, {installedExample.data(), installedExample.size()}, "\xf0\x90\x8c\x82");
    expectConverted(run, {finalSupplementary.data(), finalSupplementary.size()},
                    "\xf4\x8f\xbf\xbf");
}

void testNullEmptyAndEmbeddedNull(TestRun& run) {
    std::array<char, 16> output{};
    output.fill(static_cast<char>(0x5a));
    const std::array<char, 16> before = output;
    expectRejected(run, {nullptr, 0}, {output.data(), output.size()},
                   {kue::Utf16ToUtf8Status::NullInput, 0, 0}, before);
    expectRejected(run, {nullptr, 7}, {output.data(), output.size()},
                   {kue::Utf16ToUtf8Status::NullInput, 0, 0}, before);

    constexpr std::uint16_t emptyStorage = 0x0041;
    expectRejected(run, {&emptyStorage, 0}, {output.data(), output.size()},
                   {kue::Utf16ToUtf8Status::EmptyInput, 0, 0}, before);

    constexpr std::array<std::uint16_t, 1> leadingNull{0x0000};
    constexpr std::array<std::uint16_t, 3> middleNull{0x0041, 0x0000, 0x0042};
    constexpr std::array<std::uint16_t, 3> supplementaryThenNull{0xd83d, 0xde80, 0x0000};
    expectRejected(run, {leadingNull.data(), leadingNull.size()}, {output.data(), output.size()},
                   {kue::Utf16ToUtf8Status::EmbeddedNull, 0, 0}, before);
    expectRejected(run, {middleNull.data(), middleNull.size()}, {output.data(), output.size()},
                   {kue::Utf16ToUtf8Status::EmbeddedNull, 1, 1}, before);
    expectRejected(run, {supplementaryThenNull.data(), supplementaryThenNull.size()},
                   {output.data(), output.size()}, {kue::Utf16ToUtf8Status::EmbeddedNull, 2, 4},
                   before);
}

void testInvalidSurrogates(TestRun& run) {
    std::array<char, 16> output{};
    output.fill(static_cast<char>(0x5a));
    const std::array<char, 16> before = output;
    constexpr std::array<std::uint16_t, 1> trailingHigh{0xd800};
    constexpr std::array<std::uint16_t, 2> highThenBmp{0xdbff, 0x0041};
    constexpr std::array<std::uint16_t, 2> highThenHigh{0xd800, 0xdbff};
    constexpr std::array<std::uint16_t, 1> isolatedLow{0xdc00};
    constexpr std::array<std::uint16_t, 2> asciiThenLow{0x0041, 0xdfff};
    constexpr std::array<std::uint16_t, 2> asciiThenHigh{0x0041, 0xd800};
    expectRejected(run, {trailingHigh.data(), trailingHigh.size()}, {output.data(), output.size()},
                   {kue::Utf16ToUtf8Status::InvalidSurrogate, 0, 0}, before);
    expectRejected(run, {highThenBmp.data(), highThenBmp.size()}, {output.data(), output.size()},
                   {kue::Utf16ToUtf8Status::InvalidSurrogate, 0, 0}, before);
    expectRejected(run, {highThenHigh.data(), highThenHigh.size()}, {output.data(), output.size()},
                   {kue::Utf16ToUtf8Status::InvalidSurrogate, 0, 0}, before);
    expectRejected(run, {isolatedLow.data(), isolatedLow.size()}, {output.data(), output.size()},
                   {kue::Utf16ToUtf8Status::InvalidSurrogate, 0, 0}, before);
    expectRejected(run, {asciiThenLow.data(), asciiThenLow.size()}, {output.data(), output.size()},
                   {kue::Utf16ToUtf8Status::InvalidSurrogate, 1, 1}, before);
    expectRejected(run, {asciiThenHigh.data(), asciiThenHigh.size()},
                   {output.data(), output.size()}, {kue::Utf16ToUtf8Status::InvalidSurrogate, 1, 1},
                   before);
}

void testCapacityAndTransactionalOutput(TestRun& run) {
    constexpr std::array<std::uint16_t, 4> input{0x004b, 0x0430, 0xd83d, 0xde80};
    constexpr std::size_t requiredBytes = 7;
    std::array<char, 16> output{};
    output.fill(static_cast<char>(0x5a));
    const std::array<char, 16> before = output;
    expectRejected(run, {input.data(), input.size()}, {output.data(), requiredBytes - 1},
                   {kue::Utf16ToUtf8Status::OutputCapacityExceeded, input.size(), requiredBytes},
                   before);
    expectRejected(run, {input.data(), input.size()}, {output.data(), 0},
                   {kue::Utf16ToUtf8Status::OutputCapacityExceeded, input.size(), requiredBytes},
                   before);
    const kue::Utf16ToUtf8Result missingOutput =
        kue::convertUtf16ToUtf8({input.data(), input.size()}, {nullptr, requiredBytes});
    run.expect(missingOutput.status == kue::Utf16ToUtf8Status::OutputCapacityExceeded,
               "a null output reports unavailable capacity");
    run.expect(missingOutput.validatedCodeUnits == input.size() &&
                   missingOutput.utf8Bytes == requiredBytes,
               "a null output still reports exact validated and required counts");

    constexpr std::array<std::uint16_t, 2> invalidAfterAscii{0x0041, 0xdc00};
    expectRejected(run, {invalidAfterAscii.data(), invalidAfterAscii.size()}, {output.data(), 0},
                   {kue::Utf16ToUtf8Status::InvalidSurrogate, 1, 1}, before);
    expectConverted(run, {input.data(), input.size()}, "K\xd0\xb0\xf0\x9f\x9a\x80");
}

void initializeWorkload(std::array<std::uint16_t, kManagedCodeUnitCapacity>& input) {
    for (std::size_t index = 0; index < input.size(); index += 4) {
        input[index] = 0x004b;
        input[index + 1] = 0xd83d;
        input[index + 2] = 0xde80;
        input[index + 3] = 0x4e8c;
    }
}

void testConversionAllocation(TestRun& run) {
    std::array<std::uint16_t, kManagedCodeUnitCapacity> input{};
    std::array<char, kMaximumUtf8Bytes> output{};
    initializeWorkload(input);
    bool converted = true;
    std::uint64_t checksum = 0;
    gAllocations.store(0, std::memory_order_relaxed);
    gDeallocations.store(0, std::memory_order_relaxed);
    gMeasureAllocations.store(true, std::memory_order_relaxed);
    for (std::size_t iteration = 0; iteration < 5000; ++iteration) {
        input[0] = static_cast<std::uint16_t>(0x0041U + iteration % 26U);
        const kue::Utf16ToUtf8Result result =
            kue::convertUtf16ToUtf8({input.data(), input.size()}, {output.data(), output.size()});
        converted = result.status == kue::Utf16ToUtf8Status::Success &&
                    result.validatedCodeUnits == input.size() && result.utf8Bytes == 1024 &&
                    converted;
        checksum += static_cast<unsigned char>(output[0]);
        checksum += static_cast<unsigned char>(output[result.utf8Bytes - 1]);
    }
    gMeasureAllocations.store(false, std::memory_order_relaxed);
    run.expect(converted && checksum != 0,
               "the maximum managed-string workload converts with exact counts");
    run.expect(gAllocations.load(std::memory_order_relaxed) == 0,
               "maximum-bound UTF-16 conversion performs no heap allocation");
    run.expect(gDeallocations.load(std::memory_order_relaxed) == 0,
               "maximum-bound UTF-16 conversion performs no heap deallocation");
}

std::uint64_t hashOutput(const std::array<char, kMaximumUtf8Bytes>& output,
                         std::size_t byteCount) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (std::size_t index = 0; index < byteCount; ++index) {
        hash ^= static_cast<unsigned char>(output[index]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

void testPerformance(TestRun& run) {
    std::array<std::uint16_t, kManagedCodeUnitCapacity> input{};
    std::array<std::array<char, kMaximumUtf8Bytes>, kPerformanceBatchSize> outputs{};
    std::array<long long, kPerformanceSamples> elapsed{};
    initializeWorkload(input);
    std::uint64_t checksum = 0;
    for (std::size_t sample = 0; sample < 20; ++sample) {
        input[0] = static_cast<std::uint16_t>(0x0041U + sample % 26U);
        const kue::Utf16ToUtf8Result result = kue::convertUtf16ToUtf8(
            {input.data(), input.size()}, {outputs[0].data(), outputs[0].size()});
        checksum += hashOutput(outputs[0], result.utf8Bytes);
    }
    bool converted = true;
    for (std::size_t sample = 0; sample < elapsed.size(); ++sample) {
        const auto start = std::chrono::steady_clock::now();
        for (std::size_t batch = 0; batch < outputs.size(); ++batch) {
            input[0] = static_cast<std::uint16_t>(0x0041U + (sample + batch) % 26U);
            const kue::Utf16ToUtf8Result result = kue::convertUtf16ToUtf8(
                {input.data(), input.size()}, {outputs[batch].data(), outputs[batch].size()});
            converted = result.status == kue::Utf16ToUtf8Status::Success &&
                        result.utf8Bytes == 1024 && converted;
        }
        const auto stop = std::chrono::steady_clock::now();
        elapsed[sample] =
            std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count() /
            static_cast<long long>(outputs.size());
        for (const auto& output : outputs)
            checksum += hashOutput(output, 1024);
    }
    std::sort(elapsed.begin(), elapsed.end());
    const long long p50 = elapsed[elapsed.size() / 2];
    const long long p95 = elapsed[(elapsed.size() * 95) / 100];
    const long long p99 = elapsed[(elapsed.size() * 99) / 100];
    std::cout << "utf16_utf8_p50_ns=" << p50 << " utf16_utf8_p95_ns=" << p95
              << " utf16_utf8_p99_ns=" << p99 << " checksum=" << checksum << '\n';
    run.expect(converted, "every measured maximum-bound conversion succeeds");
    run.expect(p95 <= kP95ThresholdNanoseconds,
               "maximum-bound conversion p95 stays within 5 microseconds");
    run.expect(p99 <= kP99ThresholdNanoseconds,
               "maximum-bound conversion p99 stays within 10 microseconds");
}

}

void* operator new(std::size_t size) {
    return allocate(size);
}

void* operator new[](std::size_t size) {
    return allocate(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    return allocateNoThrow(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    return allocateNoThrow(size);
}

void* operator new(std::size_t size, std::align_val_t alignment) {
    return allocateAligned(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
    return allocateAligned(size, static_cast<std::size_t>(alignment));
}

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    return allocateAlignedNoThrow(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    return allocateAlignedNoThrow(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* memory) noexcept {
    release(memory);
}

void operator delete[](void* memory) noexcept {
    release(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    release(memory);
}

void operator delete[](void* memory, std::size_t) noexcept {
    release(memory);
}

void operator delete(void* memory, const std::nothrow_t&) noexcept {
    release(memory);
}

void operator delete[](void* memory, const std::nothrow_t&) noexcept {
    release(memory);
}

void operator delete(void* memory, std::align_val_t) noexcept {
    release(memory);
}

void operator delete[](void* memory, std::align_val_t) noexcept {
    release(memory);
}

void operator delete(void* memory, std::size_t, std::align_val_t) noexcept {
    release(memory);
}

void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept {
    release(memory);
}

void operator delete(void* memory, std::align_val_t, const std::nothrow_t&) noexcept {
    release(memory);
}

void operator delete[](void* memory, std::align_val_t, const std::nothrow_t&) noexcept {
    release(memory);
}

int main(int argc, char** argv) {
    const bool runPerformance = argc == 2 && std::string_view(argv[1]) == "--performance";
    if (argc != 1 && !runPerformance) {
        std::cerr << "usage: Utf8Tests [--performance]\n";
        return 2;
    }

    TestRun run;
    if (runPerformance) {
        testPerformance(run);
    } else {
        testDeclaredContract(run);
        testUtf8ValidatorPreserved(run);
        testEncodingBoundaries(run);
        testNullEmptyAndEmbeddedNull(run);
        testInvalidSurrogates(run);
        testCapacityAndTransactionalOutput(run);
        testConversionAllocation(run);
    }
    return run.result();
}
