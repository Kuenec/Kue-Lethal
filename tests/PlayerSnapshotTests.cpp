#include "game/PlayerSnapshot.h"

#include "AlignedAllocation.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <span>
#include <string_view>
#include <type_traits>

namespace {

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

void recordAllocation() {
    if (gMeasureAllocations.load(std::memory_order_relaxed))
        gAllocations.fetch_add(1, std::memory_order_relaxed);
}

void* allocate(std::size_t size) {
    recordAllocation();
    if (void* memory = std::malloc(size == 0 ? 1 : size))
        return memory;
    throw std::bad_alloc();
}

void* allocateAligned(std::size_t size, std::size_t alignment) {
    recordAllocation();
    if (void* memory = kue::tests::allocateAligned(size, alignment))
        return memory;
    throw std::bad_alloc();
}

kue::PlayerSnapshotInput player(std::string_view name, std::uint64_t index) {
    return {.name = name,
            .steamId = 76561198000000000ULL + index,
            .clientId = 0xfedcba9800000000ULL + index,
            .health = 100 - static_cast<int>(index),
            .insanity = static_cast<float>(index) * 0.5F,
            .dead = index % 5 == 0,
            .local = index == 0,
            .controlled = index % 7 != 0};
}

void expectPlayer(TestRun& run, const kue::PlayerSnapshotPlayer& actual,
                  kue::PlayerSnapshotInput expected) {
    run.expect(actual.name.view() == expected.name, "a player name preserves exact UTF-8 bytes");
    run.expect(actual.name.size() == expected.name.size(), "a player name preserves byte length");
    run.expect(actual.name.c_str()[actual.name.size()] == '\0',
               "a player name remains NUL terminated");
    run.expect(actual.steamId == expected.steamId, "a player preserves its full-width Steam ID");
    run.expect(actual.clientId == expected.clientId, "a player preserves its full-width client ID");
    run.expect(actual.health == expected.health, "a player preserves health");
    run.expect(std::bit_cast<std::uint32_t>(actual.insanity) ==
                   std::bit_cast<std::uint32_t>(expected.insanity),
               "a player preserves exact insanity bits");
    run.expect(actual.dead == expected.dead, "a player preserves death state");
    run.expect(actual.local == expected.local, "a player preserves locality");
    run.expect(actual.controlled == expected.controlled, "a player preserves control state");
}

void publishSentinel(TestRun& run, kue::PlayerSnapshot& snapshot) {
    kue::PlayerSnapshotTransaction transaction;
    run.expect(transaction.report(player("published sentinel", 41)).result ==
                   kue::PlayerSnapshotReportResult::Recorded,
               "the sentinel player is staged");
    run.expect(transaction.publish(snapshot).result == kue::PlayerSnapshotPublishResult::Published,
               "the sentinel snapshot is published");
}

void expectSentinel(TestRun& run, const kue::PlayerSnapshot& snapshot) {
    run.expect(snapshot.size() == 1, "a failed transaction preserves the published count");
    if (snapshot.size() == 1)
        expectPlayer(run, snapshot[0], player("published sentinel", 41));
}

void expectRejectedName(TestRun& run, std::string_view rejected,
                        kue::PlayerSnapshotReportResult expected) {
    kue::PlayerSnapshot published;
    publishSentinel(run, published);
    kue::PlayerSnapshotTransaction transaction;
    const kue::PlayerSnapshotReportOutcome report = transaction.report(player(rejected, 2));
    run.expect(report.result == expected, "an invalid player name reports its exact cause");
    const kue::PlayerSnapshotPublishOutcome publish = transaction.publish(published);
    run.expect(publish.result == kue::PlayerSnapshotPublishResult::PriorReportFailed,
               "an invalid player name blocks publication");
    run.expect(publish.failure.result == expected,
               "failed publication retains the original player-name cause");
    expectSentinel(run, published);
}

void testDeclaredModel(TestRun& run) {
    static_assert(kue::kPlayerSnapshotCapacity == 64);
    static_assert(kue::kPlayerNameCapacity == 2048);
    static_assert(sizeof(kue::PlayerSnapshot) <= std::size_t{140} * std::size_t{1024});
    static_assert(sizeof(kue::PlayerSnapshotTransaction) <= std::size_t{140} * std::size_t{1024});
    static_assert(std::is_same_v<decltype(kue::PlayerSnapshotPlayer::steamId), std::uint64_t>);
    static_assert(std::is_same_v<decltype(kue::PlayerSnapshotPlayer::clientId), std::uint64_t>);
    static_assert(std::is_copy_constructible_v<kue::PlayerSnapshot>);
    static_assert(std::is_copy_assignable_v<kue::PlayerSnapshot>);
    static_assert(!std::is_copy_constructible_v<kue::PlayerSnapshotTransaction>);
    static_assert(!std::is_copy_assignable_v<kue::PlayerSnapshotTransaction>);
    static_assert(!std::is_polymorphic_v<kue::PlayerSnapshot>);
    static_assert(!std::is_polymorphic_v<kue::PlayerSnapshotTransaction>);
    static_assert(std::is_trivially_copyable_v<kue::PlayerSnapshot>);
    static_assert(std::is_trivially_destructible_v<kue::PlayerSnapshot>);

    kue::PlayerSnapshot snapshot;
    run.expect(snapshot.empty(), "a new player snapshot is empty");
    run.expect(snapshot.view().empty(), "a new player snapshot exposes an empty view");
}

void testExactPublicationAndIteration(TestRun& run) {
    constexpr std::string_view unicodeName = "Kue \xe2\x98\x83 \xf0\x9f\x9a\x80";
    kue::PlayerSnapshotTransaction transaction;
    const kue::PlayerSnapshotInput first = player("local", 0);
    const kue::PlayerSnapshotInput second = player(unicodeName, 1);
    const kue::PlayerSnapshotInput third = player("remote", 2);
    run.expect(transaction.report(first).entryIndex == 0,
               "the first player receives the first dense index");
    run.expect(transaction.report(second).entryIndex == 1,
               "the second player receives the next dense index");
    run.expect(transaction.report(third).entryIndex == 2,
               "the third player receives the next dense index");

    kue::PlayerSnapshot snapshot;
    const kue::PlayerSnapshotPublishOutcome publish = transaction.publish(snapshot);
    run.expect(publish.result == kue::PlayerSnapshotPublishResult::Published,
               "a complete player transaction publishes");
    run.expect(snapshot.size() == 3, "publication exposes the exact player count");
    expectPlayer(run, snapshot[0], first);
    expectPlayer(run, snapshot[1], second);
    expectPlayer(run, snapshot[2], third);

    const std::span<const kue::PlayerSnapshotPlayer> view = snapshot.view();
    run.expect(view.size() == snapshot.size(), "the read view exposes the published count");
    run.expect(view.data() == snapshot.begin(), "the read view aliases stable inline storage");
    std::size_t iterated = 0;
    std::uint64_t clientIdTotal = 0;
    for (const kue::PlayerSnapshotPlayer& current : snapshot) {
        ++iterated;
        clientIdTotal += current.clientId;
    }
    run.expect(iterated == 3, "the snapshot is directly range iterable");
    run.expect(clientIdTotal == first.clientId + second.clientId + third.clientId,
               "direct iteration observes every exact client ID in order");

    kue::PlayerSnapshot copy = snapshot;
    run.expect(copy.size() == snapshot.size(), "a snapshot copy preserves the published count");
    run.expect(copy[1].name.view() == snapshot[1].name.view(),
               "a snapshot copy preserves player-name bytes");
    run.expect(copy[1].name.c_str() != snapshot[1].name.c_str(),
               "a snapshot copy owns independent inline player-name storage");

    const kue::PlayerSnapshotInput exactNaN{
        .name = "extremes",
        .steamId = std::numeric_limits<std::uint64_t>::max(),
        .clientId = std::numeric_limits<std::uint64_t>::max() - 1,
        .health = std::numeric_limits<int>::min(),
        .insanity = std::bit_cast<float>(std::uint32_t{0x7fc01234}),
        .dead = true,
        .local = true,
        .controlled = true,
    };
    kue::PlayerSnapshotInput positiveInfinity = exactNaN;
    positiveInfinity.name = "positive infinity";
    positiveInfinity.clientId = 17;
    positiveInfinity.insanity = std::numeric_limits<float>::infinity();
    kue::PlayerSnapshotInput negativeInfinity = exactNaN;
    negativeInfinity.name = "negative infinity";
    negativeInfinity.clientId = 18;
    negativeInfinity.insanity = -std::numeric_limits<float>::infinity();
    kue::PlayerSnapshotTransaction extremeTransaction;
    run.expect(extremeTransaction.report(exactNaN).result ==
                   kue::PlayerSnapshotReportResult::Recorded,
               "full-width and exact-bit player fields are accepted");
    run.expect(extremeTransaction.report(positiveInfinity).result ==
                   kue::PlayerSnapshotReportResult::Recorded,
               "positive infinity is preserved as observed player state");
    run.expect(extremeTransaction.report(negativeInfinity).result ==
                   kue::PlayerSnapshotReportResult::Recorded,
               "negative infinity is preserved as observed player state");
    run.expect(extremeTransaction.publish(copy).result ==
                   kue::PlayerSnapshotPublishResult::Published,
               "full-width and exact-bit player fields publish");
    expectPlayer(run, copy[0], exactNaN);
    expectPlayer(run, copy[1], positiveInfinity);
    expectPlayer(run, copy[2], negativeInfinity);

    kue::PlayerSnapshotTransaction emptyTransaction;
    run.expect(emptyTransaction.publish(snapshot).result ==
                   kue::PlayerSnapshotPublishResult::Published,
               "an empty runtime observation can replace stale players");
    run.expect(snapshot.empty(), "empty publication removes every stale player");
}

void testNameBoundaries(TestRun& run) {
    std::array<char, kue::kPlayerNameCapacity> maximumName{};
    for (std::size_t index = 0; index < maximumName.size(); index += 4) {
        maximumName[index] = static_cast<char>(0xf0);
        maximumName[index + 1] = static_cast<char>(0x90);
        maximumName[index + 2] = static_cast<char>(0x80);
        maximumName[index + 3] = static_cast<char>(0x80);
    }
    kue::PlayerSnapshotTransaction maximumTransaction;
    const std::string_view maximumView(maximumName.data(), maximumName.size());
    const kue::PlayerSnapshotReportOutcome maximumReport =
        maximumTransaction.report(player(maximumView, 8));
    run.expect(maximumReport.result == kue::PlayerSnapshotReportResult::Recorded,
               "an exactly 2048-byte valid UTF-8 name is accepted");
    kue::PlayerSnapshot maximumSnapshot;
    run.expect(maximumTransaction.publish(maximumSnapshot).result ==
                   kue::PlayerSnapshotPublishResult::Published,
               "an exactly maximum-length name publishes");
    run.expect(maximumSnapshot[0].name.view() == maximumView,
               "an exactly maximum-length name preserves every byte");
    run.expect(maximumSnapshot[0].name.c_str()[kue::kPlayerNameCapacity] == '\0',
               "an exactly maximum-length name retains its terminator");

    std::array<char, kue::kPlayerNameCapacity + 1> oversizedName{};
    oversizedName.fill('x');
    kue::PlayerSnapshot published;
    publishSentinel(run, published);
    kue::PlayerSnapshotTransaction oversizedTransaction;
    run.expect(oversizedTransaction.report(player("accepted before failure", 1)).result ==
                   kue::PlayerSnapshotReportResult::Recorded,
               "a valid player can precede a rejected name");
    const kue::PlayerSnapshotReportOutcome oversized = oversizedTransaction.report(
        player(std::string_view(oversizedName.data(), oversizedName.size()), 2));
    run.expect(oversized.result == kue::PlayerSnapshotReportResult::NameTooLong,
               "the first byte beyond the name limit is rejected");
    run.expect(oversized.entryIndex == 1, "name failure identifies the exact dense index");
    run.expect(oversized.actual == oversizedName.size(),
               "name failure reports the observed byte count");
    run.expect(oversized.limit == kue::kPlayerNameCapacity,
               "name failure reports the enforced byte limit");
    const kue::PlayerSnapshotReportOutcome prior =
        oversizedTransaction.report(player("after failure", 3));
    run.expect(prior.result == kue::PlayerSnapshotReportResult::PriorReportFailed,
               "a failed transaction refuses later players");
    run.expect(prior.entryIndex == oversized.entryIndex && prior.actual == oversized.actual &&
                   prior.limit == oversized.limit,
               "a repeated report retains the first failure details");
    const kue::PlayerSnapshotPublishOutcome failed = oversizedTransaction.publish(published);
    run.expect(failed.result == kue::PlayerSnapshotPublishResult::PriorReportFailed,
               "a name overflow prevents partial publication");
    run.expect(failed.failure.result == kue::PlayerSnapshotReportResult::NameTooLong,
               "failed publication retains the name-overflow cause");
    expectSentinel(run, published);

    expectRejectedName(run, {}, kue::PlayerSnapshotReportResult::EmptyName);
    constexpr std::array<char, 3> embeddedNull{{'a', '\0', 'b'}};
    expectRejectedName(run, std::string_view(embeddedNull.data(), embeddedNull.size()),
                       kue::PlayerSnapshotReportResult::EmbeddedNull);
    constexpr std::array<char, 1> continuation{{static_cast<char>(0x80)}};
    expectRejectedName(run, std::string_view(continuation.data(), continuation.size()),
                       kue::PlayerSnapshotReportResult::InvalidUtf8);
    constexpr std::array<char, 2> truncated{{static_cast<char>(0xc2), 'x'}};
    expectRejectedName(run, std::string_view(truncated.data(), truncated.size()),
                       kue::PlayerSnapshotReportResult::InvalidUtf8);
    constexpr std::array<char, 3> surrogate{
        {static_cast<char>(0xed), static_cast<char>(0xa0), static_cast<char>(0x80)}};
    expectRejectedName(run, std::string_view(surrogate.data(), surrogate.size()),
                       kue::PlayerSnapshotReportResult::InvalidUtf8);
    constexpr std::array<char, 4> aboveUnicode{{static_cast<char>(0xf4), static_cast<char>(0x90),
                                                static_cast<char>(0x80), static_cast<char>(0x80)}};
    expectRejectedName(run, std::string_view(aboveUnicode.data(), aboveUnicode.size()),
                       kue::PlayerSnapshotReportResult::InvalidUtf8);
}

void testCapacityAndTransactionalPreservation(TestRun& run) {
    kue::PlayerSnapshot published;
    publishSentinel(run, published);
    kue::PlayerSnapshotTransaction transaction;
    for (std::size_t index = 0; index < kue::kPlayerSnapshotCapacity; ++index) {
        const kue::PlayerSnapshotReportOutcome report = transaction.report(player("player", index));
        run.expect(report.result == kue::PlayerSnapshotReportResult::Recorded,
                   "every declared-capacity player is accepted");
        run.expect(report.entryIndex == index, "every accepted player receives a dense index");
    }
    const kue::PlayerSnapshotReportOutcome overflow = transaction.report(player("overflow", 64));
    run.expect(overflow.result == kue::PlayerSnapshotReportResult::CapacityExceeded,
               "the first player beyond declared capacity is rejected");
    run.expect(overflow.entryIndex == kue::kPlayerSnapshotCapacity,
               "capacity failure identifies the first unavailable index");
    run.expect(overflow.actual == kue::kPlayerSnapshotCapacity + 1,
               "capacity failure reports the attempted count");
    run.expect(overflow.limit == kue::kPlayerSnapshotCapacity,
               "capacity failure reports the enforced player limit");
    const kue::PlayerSnapshotPublishOutcome publish = transaction.publish(published);
    run.expect(publish.result == kue::PlayerSnapshotPublishResult::PriorReportFailed,
               "a capacity failure prevents partial publication");
    run.expect(publish.failure.result == kue::PlayerSnapshotReportResult::CapacityExceeded,
               "failed publication retains the capacity cause");
    expectSentinel(run, published);
}

using PlayerNameStorage =
    std::array<std::array<char, kue::kPlayerNameCapacity>, kue::kPlayerSnapshotCapacity>;

void initializeNames(PlayerNameStorage& names) {
    for (std::size_t playerIndex = 0; playerIndex < names.size(); ++playerIndex) {
        for (std::size_t byteIndex = 0; byteIndex < names[playerIndex].size(); ++byteIndex) {
            names[playerIndex][byteIndex] = static_cast<char>('a' + (playerIndex + byteIndex) % 26);
        }
    }
}

std::uint64_t buildCopyAndRead(const PlayerNameStorage& names, std::size_t sample,
                               kue::PlayerSnapshot& published) {
    kue::PlayerSnapshotTransaction transaction;
    for (std::size_t index = 0; index < names.size(); ++index) {
        kue::PlayerSnapshotInput input =
            player(std::string_view(names[index].data(), names[index].size()), index);
        input.health += static_cast<int>(sample % 7);
        if (transaction.report(input).result != kue::PlayerSnapshotReportResult::Recorded)
            return 0;
    }
    if (transaction.publish(published).result != kue::PlayerSnapshotPublishResult::Published)
        return 0;
    kue::PlayerSnapshot readCopy = published;
    std::uint64_t checksum = 0;
    for (const kue::PlayerSnapshotPlayer& current : readCopy) {
        checksum += current.clientId;
        checksum += static_cast<std::uint64_t>(current.name.size());
        checksum += static_cast<unsigned char>(current.name.c_str()[sample % current.name.size()]);
    }
    return checksum;
}

void testAllocation(TestRun& run) {
    PlayerNameStorage names{};
    initializeNames(names);
    kue::PlayerSnapshot published;
    const std::size_t allocationsBefore = gAllocations.load(std::memory_order_relaxed);
    gMeasureAllocations.store(true, std::memory_order_relaxed);
    const std::uint64_t checksum = buildCopyAndRead(names, 0, published);
    gMeasureAllocations.store(false, std::memory_order_relaxed);
    const std::size_t measuredAllocations =
        gAllocations.load(std::memory_order_relaxed) - allocationsBefore;
    run.expect(checksum != 0, "the measured maximum-player workload completes successfully");
    run.expect(measuredAllocations == 0,
               "a maximum player build, publication, copy, and read performs no heap allocation");
}

void testPerformance(TestRun& run) {
    constexpr std::size_t sampleCount = 2000;
    constexpr std::size_t warmupCount = 20;
    constexpr long long p99ThresholdNanoseconds = 100000;
    PlayerNameStorage names{};
    initializeNames(names);
    kue::PlayerSnapshot published;
    std::uint64_t checksum = 0;
    for (std::size_t sample = 0; sample < warmupCount; ++sample)
        checksum += buildCopyAndRead(names, sample, published);
    std::array<long long, sampleCount> elapsed{};
    for (std::size_t sample = 0; sample < sampleCount; ++sample) {
        const auto start = std::chrono::steady_clock::now();
        checksum += buildCopyAndRead(names, sample + warmupCount, published);
        const auto stop = std::chrono::steady_clock::now();
        elapsed[sample] =
            std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count();
    }
    std::sort(elapsed.begin(), elapsed.end());
    const long long p99 = elapsed[(sampleCount * 99) / 100];
    std::cout << "player_snapshot_p50_ns=" << elapsed[sampleCount / 2]
              << " player_snapshot_p99_ns=" << p99 << " checksum=" << checksum << '\n';
    run.expect(p99 <= p99ThresholdNanoseconds,
               "64-player build, publication, copy, and read p99 stays within 100 microseconds");
}

}

void* operator new(std::size_t size) {
    return allocate(size);
}

void* operator new[](std::size_t size) {
    return allocate(size);
}

void* operator new(std::size_t size, std::align_val_t alignment) {
    return allocateAligned(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
    return allocateAligned(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete[](void* memory) noexcept {
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    std::free(memory);
}

void operator delete[](void* memory, std::size_t) noexcept {
    std::free(memory);
}

void operator delete(void* memory, std::align_val_t) noexcept {
    kue::tests::releaseAligned(memory);
}

void operator delete[](void* memory, std::align_val_t) noexcept {
    kue::tests::releaseAligned(memory);
}

void operator delete(void* memory, std::size_t, std::align_val_t) noexcept {
    kue::tests::releaseAligned(memory);
}

void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept {
    kue::tests::releaseAligned(memory);
}

int main(int argc, char** argv) {
    const bool runPerformance = argc == 2 && std::string_view(argv[1]) == "--performance";
    if (argc != 1 && !runPerformance) {
        std::cerr << "usage: PlayerSnapshotTests [--performance]\n";
        return 2;
    }

    TestRun run;
    testDeclaredModel(run);
    testExactPublicationAndIteration(run);
    testNameBoundaries(run);
    testCapacityAndTransactionalPreservation(run);
    testAllocation(run);
    if (runPerformance)
        testPerformance(run);
    return run.result();
}
