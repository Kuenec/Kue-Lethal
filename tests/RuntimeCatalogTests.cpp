#include "game/PlayerActions.h"

#include "AlignedAllocation.h"
#include "game/RuntimeCatalog.h"

#include <array>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
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
kue::RuntimeCatalogs gCatalogs;

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

struct AlignedAllocationRequest {
    std::size_t size;
    std::size_t alignment;
};

void* allocateAligned(AlignedAllocationRequest request) {
    recordAllocation();
    if (void* memory = kue::tests::allocateAligned(request.size, request.alignment))
        return memory;
    throw std::bad_alloc();
}

kue::RuntimeAssetIdentity asset(std::uint64_t value) {
    return kue::RuntimeAssetIdentity{value};
}

kue::OrderedCatalogName name(std::string_view value) {
    return kue::OrderedCatalogName{value};
}

struct NameIndex {
    char prefix;
    std::size_t value;
};

std::string_view indexedName(std::array<char, 32>& storage, NameIndex index) {
    storage[0] = index.prefix;
    storage[1] = '-';
    const auto converted =
        std::to_chars(storage.data() + 2, storage.data() + storage.size(), index.value);
    if (converted.ec != std::errc{})
        return {};
    return {storage.data(), static_cast<std::size_t>(converted.ptr - storage.data())};
}

struct ExpectedEnemy {
    std::size_t index;
    std::uint64_t identity;
    std::string_view name;
    std::uint32_t activeCount;
};

struct ExpectedItem {
    std::size_t index;
    std::uint64_t identity;
    std::string_view name;
};

void expectEnemy(TestRun& run, ExpectedEnemy expected) {
    const kue::EnemyCatalogLookup lookup = gCatalogs.enemyAt(expected.index);
    run.expect(lookup.result == kue::CatalogLookupResult::Found,
               "a committed enemy entry is found");
    run.expect(lookup.entry.asset == asset(expected.identity),
               "an enemy entry preserves asset identity");
    run.expect(lookup.entry.name == expected.name, "an enemy entry preserves ordered UTF-8 bytes");
    run.expect(lookup.entry.activeCount == expected.activeCount,
               "an enemy entry preserves its active count");
    run.expect(lookup.entry.id.generation == gCatalogs.enemyGeneration(),
               "an enemy entry carries the current enemy generation");
    run.expect(lookup.entry.id.index == expected.index, "an enemy entry carries its dense index");
}

void expectItem(TestRun& run, ExpectedItem expected) {
    const kue::ItemCatalogLookup lookup = gCatalogs.itemAt(expected.index);
    run.expect(lookup.result == kue::CatalogLookupResult::Found, "a committed item entry is found");
    run.expect(lookup.entry.asset == asset(expected.identity),
               "an item entry preserves asset identity");
    run.expect(lookup.entry.name == expected.name, "an item entry preserves ordered UTF-8 bytes");
    run.expect(lookup.entry.id.generation == gCatalogs.itemGeneration(),
               "an item entry carries the current item generation");
    run.expect(lookup.entry.id.index == expected.index, "an item entry carries its dense index");
}

void expectInvalidUtf8(TestRun& run, std::string_view invalidName, std::uint64_t identity) {
    run.expect(gCatalogs.beginItemCatalog() == kue::CatalogBeginResult::Begun,
               "an invalid-UTF-8 transaction begins");
    run.expect(gCatalogs.reportItem(asset(identity), name(invalidName)).result ==
                   kue::CatalogReportResult::InvalidUtf8,
               "a malformed UTF-8 sequence is rejected");
    run.expect(gCatalogs.commitItemCatalog().failure.result ==
                   kue::CatalogReportResult::InvalidUtf8,
               "commit retains the invalid-UTF-8 cause");
}

void commitInstalledCatalogs(TestRun& run) {
    constexpr std::size_t enemyCount = 33;
    constexpr std::size_t itemCount = 90;
    run.expect(gCatalogs.beginEnemyCatalog() == kue::CatalogBeginResult::Begun,
               "the installed enemy transaction begins");
    for (std::size_t index = 0; index < enemyCount; ++index) {
        std::array<char, 32> storage{};
        const std::string_view value = indexedName(storage, {'e', index});
        run.expect(gCatalogs.reportEnemy(asset(index + 1), name(value)).result ==
                       kue::CatalogReportResult::Recorded,
                   "every installed enemy entry is recorded densely");
    }
    run.expect(gCatalogs.commitEnemyCatalog().result == kue::CatalogCommitResult::Committed,
               "the installed enemy transaction commits");

    run.expect(gCatalogs.beginItemCatalog() == kue::CatalogBeginResult::Begun,
               "the installed item transaction begins");
    for (std::size_t index = 0; index < itemCount; ++index) {
        std::array<char, 32> storage{};
        const std::string_view value = indexedName(storage, {'i', index});
        run.expect(gCatalogs.reportItem(asset(1000 + index), name(value)).result ==
                       kue::CatalogReportResult::Recorded,
                   "every installed item entry is recorded densely");
    }
    run.expect(gCatalogs.commitItemCatalog().result == kue::CatalogCommitResult::Committed,
               "the installed item transaction commits");
}

void testDeclaredStorageAndGeneration(TestRun& run) {
    static_assert(kue::kRuntimeCatalogCapacity == 256);
    static_assert(kue::kRuntimeCatalogNameCapacity == 2048);
    static_assert(kue::kRuntimeCatalogTextCapacity == 524288);
    static_assert(sizeof(kue::RuntimeCatalogs) <= std::size_t{1600} * std::size_t{1024});
    static_assert(!std::is_copy_constructible_v<kue::RuntimeCatalogs>);
    static_assert(!std::is_copy_assignable_v<kue::RuntimeCatalogs>);
    static_assert(!std::is_move_constructible_v<kue::RuntimeCatalogs>);
    static_assert(!std::is_move_assignable_v<kue::RuntimeCatalogs>);
    static_assert(!std::is_same_v<kue::EnemyTypeId, kue::ItemTypeId>);

    run.expect(gCatalogs.enemyCount() == 0 && gCatalogs.itemCount() == 0,
               "a new catalog owner starts empty");
    run.expect(gCatalogs.enemyGeneration().value == 0 && gCatalogs.itemGeneration().value == 0,
               "an empty catalog owner has invalid generations");
}

void testSignedRuntimeIdentityMapping(TestRun& run) {
    constexpr kue::RuntimeAssetIdentity negative =
        kue::runtimeAssetIdentityFromSigned32(std::numeric_limits<std::int32_t>::min());
    constexpr kue::RuntimeAssetIdentity positive =
        kue::runtimeAssetIdentityFromSigned32(std::numeric_limits<std::int32_t>::max());
    run.expect(kue::runtimeAssetIdentityFromSigned32(0).value == 0,
               "the invalid Unity instance ID remains invalid");
    run.expect(kue::runtimeAssetIdentityFromSigned32(-1).value == 4294967296ULL,
               "a negative Unity instance ID maps without sign loss");
    run.expect(kue::runtimeAssetIdentityFromSigned32(1).value == 2,
               "a positive Unity instance ID maps without colliding with zero");
    run.expect(negative.value != 0 && positive.value != 0 && negative != positive,
               "signed 32-bit endpoint identities remain nonzero and distinct");
}

void testActionResolutionRejectsStaleCatalogIds(TestRun& run) {
    gCatalogs.resetForSessionEnd();
    run.expect(gCatalogs.beginEnemyCatalog() == kue::CatalogBeginResult::Begun,
               "an action-resolution enemy catalog begins");
    run.expect(gCatalogs.reportEnemy(asset(1), name("old enemy")).result ==
                   kue::CatalogReportResult::Recorded,
               "an action-resolution enemy entry is staged");
    run.expect(gCatalogs.commitEnemyCatalog().result == kue::CatalogCommitResult::Committed,
               "an action-resolution enemy catalog commits");
    const kue::EnemyTypeId selected = gCatalogs.enemyAt(0).entry.id;
    const kue::PlayerActionRequest request{
        kue::PlayerAction::SpawnEnemy,
        kue::EnemySpawnPayload{selected, 1U, kue::EnemySpawnArea::Inside}};
    const kue::PlayerActionCatalogResolution current =
        kue::resolvePlayerActionCatalog(request, gCatalogs);
    run.expect(current.result == kue::PlayerActionCatalogResolutionResult::Resolved &&
                   current.index == 0,
               "a current generation-bearing action resolves its exact executor index");

    run.expect(gCatalogs.beginEnemyCatalog() == kue::CatalogBeginResult::Begun,
               "an action-resolution replacement catalog begins");
    run.expect(gCatalogs.reportEnemy(asset(2), name("new enemy")).result ==
                   kue::CatalogReportResult::Recorded,
               "an action-resolution replacement entry is staged");
    run.expect(gCatalogs.commitEnemyCatalog().result == kue::CatalogCommitResult::Committed,
               "an action-resolution replacement catalog commits");
    run.expect(kue::resolvePlayerActionCatalog(request, gCatalogs).result ==
                   kue::PlayerActionCatalogResolutionResult::StaleGeneration,
               "an old selected action is rejected immediately before executor index encoding");
}

void testDenseTransactionsAndTypedLookup(TestRun& run) {
    gCatalogs.resetForSessionEnd();
    run.expect(gCatalogs.beginEnemyCatalog() == kue::CatalogBeginResult::Begun,
               "an enemy transaction begins");
    run.expect(gCatalogs.beginItemCatalog() == kue::CatalogBeginResult::TransactionInProgress,
               "a second identity transaction is refused");
    run.expect(gCatalogs.reportEnemy(asset(11), name("alpha")).result ==
                   kue::CatalogReportResult::Recorded,
               "the first enemy is recorded at the first dense index");
    run.expect(gCatalogs.reportEnemy(asset(12), name("zeta")).result ==
                   kue::CatalogReportResult::Recorded,
               "the second enemy is recorded at the next dense index");
    run.expect(gCatalogs.commitEnemyCatalog().result == kue::CatalogCommitResult::Committed,
               "a complete enemy transaction commits once");
    run.expect(gCatalogs.enemyCount() == 2, "an enemy commit publishes its exact dense size");
    expectEnemy(run, {0, 11, "alpha", 0});
    expectEnemy(run, {1, 12, "zeta", 0});
    run.expect(gCatalogs.enemyAt(2).result == kue::CatalogLookupResult::IndexOutOfRange,
               "the first enemy index beyond the live size is rejected");
    const kue::EnemyTypeId firstId = gCatalogs.enemyAt(0).entry.id;
    run.expect(gCatalogs.enemy(firstId).result == kue::CatalogLookupResult::Found,
               "a current typed enemy ID resolves");
    run.expect(
        gCatalogs.enemy(kue::EnemyTypeId{kue::EnemyCatalogGeneration{0}, firstId.index}).result ==
            kue::CatalogLookupResult::StaleGeneration,
        "a stale typed enemy ID is rejected");

    run.expect(gCatalogs.beginItemCatalog() == kue::CatalogBeginResult::Begun,
               "an item transaction begins after the enemy commit");
    run.expect(gCatalogs.reportItem(asset(101), name("flashlight")).result ==
                   kue::CatalogReportResult::Recorded,
               "an item is recorded");
    run.expect(gCatalogs.commitItemCatalog().result == kue::CatalogCommitResult::Committed,
               "an item transaction commits independently");
    expectItem(run, {0, 101, "flashlight"});
    const kue::ItemTypeId itemId = gCatalogs.itemAt(0).entry.id;
    run.expect(gCatalogs.item(itemId).result == kue::CatalogLookupResult::Found,
               "a current typed item ID resolves");
    run.expect(
        gCatalogs.item(kue::ItemTypeId{kue::ItemCatalogGeneration{0}, itemId.index}).result ==
            kue::CatalogLookupResult::StaleGeneration,
        "a stale typed item ID is rejected");
}

void testNameAndIdentityBoundaries(TestRun& run) {
    gCatalogs.resetForSessionEnd();
    std::array<char, kue::kRuntimeCatalogNameCapacity + 1> longName{};
    longName.fill('x');

    run.expect(gCatalogs.reportEnemy(asset(1), name("enemy")).result ==
                   kue::CatalogReportResult::NoTransaction,
               "reporting without a transaction is rejected");
    run.expect(gCatalogs.commitEnemyCatalog().result == kue::CatalogCommitResult::NoTransaction,
               "committing without a transaction is rejected");
    run.expect(gCatalogs.beginEnemyCatalog() == kue::CatalogBeginResult::Begun,
               "an empty transaction begins");
    run.expect(gCatalogs.commitEnemyCatalog().result == kue::CatalogCommitResult::EmptyCatalog,
               "an empty catalog cannot replace live state");
    run.expect(gCatalogs.beginEnemyCatalog() == kue::CatalogBeginResult::Begun,
               "a wrong-kind transaction begins");
    run.expect(gCatalogs.reportItem(asset(1), name("item")).result ==
                   kue::CatalogReportResult::WrongTransaction,
               "a report cannot cross catalog transaction kinds");
    run.expect(gCatalogs.abortItemCatalog() == kue::CatalogAbortResult::WrongTransaction,
               "an abort cannot target another catalog transaction");
    run.expect(gCatalogs.abortEnemyCatalog() == kue::CatalogAbortResult::Aborted,
               "the owning catalog can abort after a wrong-kind request");
    run.expect(gCatalogs.beginEnemyCatalog() == kue::CatalogBeginResult::Begun,
               "a boundary transaction begins");
    run.expect(gCatalogs.reportEnemy(asset(0), name("enemy")).result ==
                   kue::CatalogReportResult::InvalidAsset,
               "zero is rejected as an opaque asset identity");
    const kue::CatalogCommitOutcome invalidAssetCommit = gCatalogs.commitEnemyCatalog();
    run.expect(invalidAssetCommit.result == kue::CatalogCommitResult::PriorReportFailed &&
                   invalidAssetCommit.failure.result == kue::CatalogReportResult::InvalidAsset,
               "commit preserves the first invalid-asset cause");

    run.expect(gCatalogs.beginEnemyCatalog() == kue::CatalogBeginResult::Begun,
               "an empty-name transaction begins");
    run.expect(gCatalogs.reportEnemy(asset(1), name("")).result ==
                   kue::CatalogReportResult::EmptyName,
               "an empty display name is rejected");
    run.expect(gCatalogs.reportEnemy(asset(2), name("later")).result ==
                   kue::CatalogReportResult::PriorReportFailed,
               "a poisoned transaction rejects later reports");
    run.expect(gCatalogs.commitEnemyCatalog().failure.result == kue::CatalogReportResult::EmptyName,
               "a poisoned commit retains the first empty-name cause");

    constexpr char embeddedNull[] = {'a', '\0', 'b'};
    run.expect(gCatalogs.beginItemCatalog() == kue::CatalogBeginResult::Begun,
               "an embedded-null transaction begins");
    run.expect(
        gCatalogs.reportItem(asset(3), name(std::string_view{embeddedNull, sizeof(embeddedNull)}))
                .result == kue::CatalogReportResult::EmbeddedNull,
        "an embedded null is rejected before storage");
    run.expect(gCatalogs.commitItemCatalog().failure.result ==
                   kue::CatalogReportResult::EmbeddedNull,
               "commit retains the embedded-null cause");

    constexpr std::array<char, 2> overlongUtf8 = {static_cast<char>(0xc0), static_cast<char>(0xaf)};
    constexpr std::array<char, 2> truncatedUtf8 = {static_cast<char>(0xe2),
                                                   static_cast<char>(0x82)};
    constexpr std::array<char, 3> surrogateUtf8 = {static_cast<char>(0xed), static_cast<char>(0xa0),
                                                   static_cast<char>(0x80)};
    constexpr std::array<char, 4> outOfRangeUtf8 = {
        static_cast<char>(0xf4), static_cast<char>(0x90), static_cast<char>(0x80),
        static_cast<char>(0x80)};
    expectInvalidUtf8(run, {overlongUtf8.data(), overlongUtf8.size()}, 4);
    expectInvalidUtf8(run, {truncatedUtf8.data(), truncatedUtf8.size()}, 5);
    expectInvalidUtf8(run, {surrogateUtf8.data(), surrogateUtf8.size()}, 6);
    expectInvalidUtf8(run, {outOfRangeUtf8.data(), outOfRangeUtf8.size()}, 7);

    constexpr std::array<char, 9> validUtf8 = {
        static_cast<char>(0xc2), static_cast<char>(0xa2), static_cast<char>(0xe2),
        static_cast<char>(0x82), static_cast<char>(0xac), static_cast<char>(0xf0),
        static_cast<char>(0x9f), static_cast<char>(0x98), static_cast<char>(0x80)};
    run.expect(gCatalogs.beginItemCatalog() == kue::CatalogBeginResult::Begun,
               "a valid multibyte UTF-8 transaction begins");
    run.expect(gCatalogs.reportItem(asset(8), name({validUtf8.data(), validUtf8.size()})).result ==
                   kue::CatalogReportResult::Recorded,
               "valid two-, three-, and four-byte UTF-8 sequences are accepted");
    run.expect(gCatalogs.commitItemCatalog().result == kue::CatalogCommitResult::Committed,
               "valid multibyte UTF-8 commits");

    run.expect(gCatalogs.beginItemCatalog() == kue::CatalogBeginResult::Begun,
               "an overlong-name transaction begins");
    run.expect(
        gCatalogs.reportItem(asset(4), name(std::string_view{longName.data(), longName.size()}))
                .result == kue::CatalogReportResult::NameTooLong,
        "a 2,049-byte name is rejected");
    run.expect(gCatalogs.commitItemCatalog().failure.limit == kue::kRuntimeCatalogNameCapacity,
               "an overlong failure preserves the exact byte limit");

    run.expect(gCatalogs.beginEnemyCatalog() == kue::CatalogBeginResult::Begun,
               "a duplicate transaction begins");
    run.expect(gCatalogs.reportEnemy(asset(7), name("bracken")).result ==
                   kue::CatalogReportResult::Recorded,
               "a unique asset and name are recorded");
    run.expect(gCatalogs.reportEnemy(asset(7), name("bracken")).result ==
                   kue::CatalogReportResult::AlreadyRecorded,
               "the same asset and name are idempotent");
    run.expect(gCatalogs.reportEnemy(asset(8), name("bracken")).result ==
                   kue::CatalogReportResult::DuplicateName,
               "a distinct asset with the same validated display name is rejected");
    run.expect(gCatalogs.commitEnemyCatalog().failure.result ==
                   kue::CatalogReportResult::DuplicateName,
               "a duplicate-name commit preserves its cause");

    run.expect(gCatalogs.beginEnemyCatalog() == kue::CatalogBeginResult::Begun,
               "an asset-conflict transaction begins");
    run.expect(gCatalogs.reportEnemy(asset(9), name("coil-head")).result ==
                   kue::CatalogReportResult::Recorded,
               "an asset conflict fixture records its first name");
    run.expect(gCatalogs.reportEnemy(asset(9), name("coil head")).result ==
                   kue::CatalogReportResult::AssetNameConflict,
               "one asset cannot acquire a different display name in one transaction");
    run.expect(gCatalogs.commitEnemyCatalog().failure.result ==
                   kue::CatalogReportResult::AssetNameConflict,
               "an asset-name conflict remains actionable at commit");

    run.expect(gCatalogs.beginItemCatalog() == kue::CatalogBeginResult::Begun,
               "an exact-limit transaction begins");
    run.expect(
        gCatalogs
                .reportItem(asset(10), name(std::string_view{longName.data(),
                                                             kue::kRuntimeCatalogNameCapacity}))
                .result == kue::CatalogReportResult::Recorded,
        "a 2,048-byte name is accepted");
    run.expect(gCatalogs.commitItemCatalog().result == kue::CatalogCommitResult::Committed,
               "an exact-limit name commits");
    run.expect(gCatalogs.itemAt(0).entry.name.size() == kue::kRuntimeCatalogNameCapacity,
               "an exact-limit name retains every byte");
}

void testFailurePreservesLiveCatalog(TestRun& run) {
    gCatalogs.resetForSessionEnd();
    run.expect(gCatalogs.beginEnemyCatalog() == kue::CatalogBeginResult::Begun,
               "the preservation baseline begins");
    run.expect(gCatalogs.reportEnemy(asset(1), name("baboon hawk")).result ==
                   kue::CatalogReportResult::Recorded,
               "the preservation baseline records its first entry");
    run.expect(gCatalogs.reportEnemy(asset(2), name("bracken")).result ==
                   kue::CatalogReportResult::Recorded,
               "the preservation baseline records its second entry");
    run.expect(gCatalogs.commitEnemyCatalog().result == kue::CatalogCommitResult::Committed,
               "the preservation baseline commits");
    const kue::EnemyCatalogGeneration baselineGeneration = gCatalogs.enemyGeneration();
    const char* const baselineAddress = gCatalogs.enemyAt(0).entry.name.data();

    run.expect(gCatalogs.beginEnemyCatalog() == kue::CatalogBeginResult::Begun,
               "a middle-failure transaction begins");
    run.expect(gCatalogs.reportEnemy(asset(3), name("butler")).result ==
                   kue::CatalogReportResult::Recorded,
               "a middle-failure transaction stages an entry");
    run.expect(gCatalogs.reportEnemy(asset(0), name("invalid")).result ==
                   kue::CatalogReportResult::InvalidAsset,
               "a middle report fails explicitly");
    const kue::CatalogCommitOutcome failed = gCatalogs.commitEnemyCatalog();
    run.expect(failed.result == kue::CatalogCommitResult::PriorReportFailed,
               "a failed transaction cannot publish its prefix");
    run.expect(gCatalogs.enemyGeneration() == baselineGeneration,
               "a failed transaction preserves the live generation");
    run.expect(gCatalogs.enemyCount() == 2, "a failed transaction preserves the live size");
    run.expect(gCatalogs.enemyAt(0).entry.name.data() == baselineAddress,
               "a failed transaction preserves live name storage");
    expectEnemy(run, {0, 1, "baboon hawk", 0});
    expectEnemy(run, {1, 2, "bracken", 0});

    run.expect(gCatalogs.beginEnemyCatalog() == kue::CatalogBeginResult::Begun,
               "an abortable transaction begins");
    run.expect(gCatalogs.reportEnemy(asset(4), name("masked")).result ==
                   kue::CatalogReportResult::Recorded,
               "an abortable transaction stages an entry");
    run.expect(gCatalogs.abortEnemyCatalog() == kue::CatalogAbortResult::Aborted,
               "an explicit abort discards staging");
    run.expect(gCatalogs.enemyGeneration() == baselineGeneration && gCatalogs.enemyCount() == 2,
               "an explicit abort preserves the live catalog");
    run.expect(gCatalogs.abortEnemyCatalog() == kue::CatalogAbortResult::NoTransaction,
               "an abort without a transaction is explicit");
}

void testInstalledWorkloadAndActivity(TestRun& run) {
    gCatalogs.resetForSessionEnd();
    run.expect(gCatalogs.beginEnemyActivity(kue::EnemyCatalogGeneration{0}) ==
                   kue::EnemyActivityBeginResult::NoCatalog,
               "activity cannot begin before an enemy catalog exists");
    run.expect(gCatalogs.commitEnemyActivity().result ==
                   kue::EnemyActivityCommitResult::NoTransaction,
               "activity cannot commit without a transaction");
    commitInstalledCatalogs(run);
    run.expect(gCatalogs.enemyCount() == 33, "the installed enemy workload has 33 entries");
    run.expect(gCatalogs.itemCount() == 90, "the installed item workload has 90 entries");
    std::array<char, 32> enemyName{};
    std::array<char, 32> itemName{};
    expectEnemy(run, {32, 33, indexedName(enemyName, {'e', 32}), 0});
    expectItem(run, {89, 1089, indexedName(itemName, {'i', 89})});

    const kue::EnemyCatalogGeneration generation = gCatalogs.enemyGeneration();
    run.expect(gCatalogs.beginEnemyActivity(kue::EnemyCatalogGeneration{0}) ==
                   kue::EnemyActivityBeginResult::StaleGeneration,
               "activity rejects a stale catalog generation");
    run.expect(gCatalogs.beginEnemyActivity(generation) == kue::EnemyActivityBeginResult::Begun,
               "activity begins against the exact live generation");
    const kue::CatalogReportOutcome wrongTransaction =
        gCatalogs.reportItem(asset(7), name("unrelated item"));
    run.expect(wrongTransaction.result == kue::CatalogReportResult::WrongTransaction &&
                   wrongTransaction.entryIndex == 0 && wrongTransaction.actual == 0 &&
                   wrongTransaction.limit == 0,
               "a wrong-kind report cannot expose stale catalog staging details");
    run.expect(gCatalogs.beginItemCatalog() == kue::CatalogBeginResult::TransactionInProgress,
               "identity replacement cannot overlap activity staging");
    const kue::EnemyTypeId firstId = gCatalogs.enemyAt(0).entry.id;
    const kue::EnemyTypeId lastId = gCatalogs.enemyAt(32).entry.id;
    run.expect(gCatalogs.reportEnemyActivity(firstId, 3).result ==
                   kue::EnemyActivityReportResult::Recorded,
               "activity records a checked count");
    run.expect(gCatalogs.reportEnemyActivity(lastId, 2).result ==
                   kue::EnemyActivityReportResult::Recorded,
               "activity records the final installed enemy index");
    run.expect(gCatalogs.commitEnemyActivity().result == kue::EnemyActivityCommitResult::Changed,
               "changed activity publishes once");
    expectEnemy(run, {0, 1, "e-0", 3});
    expectEnemy(run, {32, 33, "e-32", 2});

    run.expect(gCatalogs.beginEnemyActivity(generation) == kue::EnemyActivityBeginResult::Begun,
               "an identical activity transaction begins");
    run.expect(gCatalogs.reportEnemyActivity(firstId, 3).result ==
                   kue::EnemyActivityReportResult::Recorded,
               "identical activity records its first count");
    run.expect(gCatalogs.reportEnemyActivity(lastId, 2).result ==
                   kue::EnemyActivityReportResult::Recorded,
               "identical activity records its final count");
    run.expect(gCatalogs.commitEnemyActivity().result == kue::EnemyActivityCommitResult::Unchanged,
               "identical activity does not publish a false change");

    run.expect(gCatalogs.beginEnemyActivity(generation) == kue::EnemyActivityBeginResult::Begun,
               "a zero-count activity transaction begins");
    run.expect(gCatalogs.reportEnemyActivity(firstId, 0).result ==
                   kue::EnemyActivityReportResult::ZeroCount,
               "a zero activity report is rejected explicitly");
    run.expect(gCatalogs.commitEnemyActivity().failure.result ==
                   kue::EnemyActivityReportResult::ZeroCount,
               "a zero-count failure remains actionable at commit");

    run.expect(gCatalogs.beginEnemyActivity(generation) == kue::EnemyActivityBeginResult::Begun,
               "a stale-ID activity transaction begins");
    run.expect(gCatalogs
                       .reportEnemyActivity(
                           kue::EnemyTypeId{kue::EnemyCatalogGeneration{0}, firstId.index}, 1)
                       .result == kue::EnemyActivityReportResult::StaleGeneration,
               "activity rejects a stale generation-bearing enemy ID");
    run.expect(gCatalogs.commitEnemyActivity().failure.result ==
                   kue::EnemyActivityReportResult::StaleGeneration,
               "a stale-ID failure remains actionable at commit");

    run.expect(gCatalogs.beginEnemyActivity(generation) == kue::EnemyActivityBeginResult::Begun,
               "an overflow activity transaction begins");
    run.expect(
        gCatalogs.reportEnemyActivity(firstId, std::numeric_limits<std::uint32_t>::max()).result ==
            kue::EnemyActivityReportResult::CountOverflow,
        "the reserved maximum active count is rejected explicitly");
    run.expect(gCatalogs.commitEnemyActivity().failure.result ==
                   kue::EnemyActivityReportResult::CountOverflow,
               "a maximum-count failure remains actionable at commit");

    run.expect(gCatalogs.beginEnemyActivity(generation) == kue::EnemyActivityBeginResult::Begun,
               "an accumulated overflow activity transaction begins");
    run.expect(
        gCatalogs.reportEnemyActivity(firstId, std::numeric_limits<std::uint32_t>::max() - 1U)
                .result == kue::EnemyActivityReportResult::Recorded,
        "the largest supported active count is representable");
    run.expect(gCatalogs.reportEnemyActivity(firstId, 1).result ==
                   kue::EnemyActivityReportResult::CountOverflow,
               "an accumulated reserved maximum is rejected instead of wrapping");
    const kue::EnemyActivityCommitOutcome overflowCommit = gCatalogs.commitEnemyActivity();
    run.expect(overflowCommit.result == kue::EnemyActivityCommitResult::PriorReportFailed &&
                   overflowCommit.failure.result == kue::EnemyActivityReportResult::CountOverflow,
               "an activity commit preserves its overflow cause");
    expectEnemy(run, {0, 1, "e-0", 3});

    run.expect(gCatalogs.beginEnemyActivity(generation) == kue::EnemyActivityBeginResult::Begun,
               "an invalid-ID activity transaction begins");
    run.expect(gCatalogs
                       .reportEnemyActivity(
                           kue::EnemyTypeId{generation, static_cast<std::uint16_t>(33)}, 1)
                       .result == kue::EnemyActivityReportResult::IndexOutOfRange,
               "activity rejects the first index beyond the live catalog");
    run.expect(gCatalogs.commitEnemyActivity().result ==
                   kue::EnemyActivityCommitResult::PriorReportFailed,
               "an invalid activity index cannot publish");

    run.expect(gCatalogs.beginEnemyCatalog() == kue::CatalogBeginResult::Begun,
               "an identical enemy identity transaction begins");
    for (std::size_t index = 0; index < 33; ++index) {
        std::array<char, 32> storage{};
        const std::string_view value = indexedName(storage, {'e', index});
        run.expect(gCatalogs.reportEnemy(asset(index + 1), name(value)).result ==
                       kue::CatalogReportResult::Recorded,
                   "an identical enemy identity entry is staged");
    }
    run.expect(gCatalogs.commitEnemyCatalog().result == kue::CatalogCommitResult::Unchanged,
               "an identical identity transaction preserves generation and activity");
    run.expect(gCatalogs.enemyGeneration() == generation,
               "an identical identity transaction preserves generation");
    expectEnemy(run, {0, 1, "e-0", 3});

    const kue::ItemCatalogGeneration itemGeneration = gCatalogs.itemGeneration();
    run.expect(gCatalogs.beginEnemyCatalog() == kue::CatalogBeginResult::Begun,
               "an enemy identity replacement begins");
    run.expect(gCatalogs.reportEnemy(asset(700), name("replacement")).result ==
                   kue::CatalogReportResult::Recorded,
               "an enemy identity replacement stages its entry");
    run.expect(gCatalogs.commitEnemyCatalog().result == kue::CatalogCommitResult::Committed,
               "an enemy identity replacement commits");
    run.expect(gCatalogs.enemyGeneration().value == generation.value + 1,
               "an enemy identity replacement advances generation exactly once");
    run.expect(gCatalogs.enemy(firstId).result == kue::CatalogLookupResult::StaleGeneration,
               "an identity replacement makes an old enemy ID stale");
    expectEnemy(run, {0, 700, "replacement", 0});
    run.expect(gCatalogs.itemGeneration() == itemGeneration,
               "an enemy identity replacement leaves item generation unchanged");
}

void testFullCapacity(TestRun& run) {
    gCatalogs.resetForSessionEnd();
    run.expect(gCatalogs.beginEnemyCatalog() == kue::CatalogBeginResult::Begun,
               "the maximum-capacity transaction begins");
    std::array<char, kue::kRuntimeCatalogNameCapacity> storage{};
    constexpr std::string_view digits = "0123456789abcdef";
    for (std::size_t index = 0; index < kue::kRuntimeCatalogCapacity; ++index) {
        storage.fill('x');
        storage[0] = digits[index / 16];
        storage[1] = digits[index % 16];
        run.expect(gCatalogs
                           .reportEnemy(asset(index + 1),
                                        name(std::string_view{storage.data(), storage.size()}))
                           .result == kue::CatalogReportResult::Recorded,
                   "every maximum-size catalog slot accepts a maximum-size name");
    }
    run.expect(gCatalogs.reportEnemy(asset(999), name("overflow")).result ==
                   kue::CatalogReportResult::CapacityExceeded,
               "the 257th catalog entry is rejected before storage");
    const kue::CatalogCommitOutcome overflow = gCatalogs.commitEnemyCatalog();
    run.expect(overflow.result == kue::CatalogCommitResult::PriorReportFailed &&
                   overflow.failure.actual == 257 &&
                   overflow.failure.limit == kue::kRuntimeCatalogCapacity,
               "capacity failure preserves exact actual and limit values");
    run.expect(gCatalogs.enemyCount() == 0 && gCatalogs.enemyGeneration().value == 0,
               "an over-capacity initial transaction publishes nothing");

    run.expect(gCatalogs.beginEnemyCatalog() == kue::CatalogBeginResult::Begun,
               "a full valid transaction begins after rejection");
    for (std::size_t index = 0; index < kue::kRuntimeCatalogCapacity; ++index) {
        storage.fill('y');
        storage[0] = digits[index / 16];
        storage[1] = digits[index % 16];
        run.expect(gCatalogs
                           .reportEnemy(asset(index + 1),
                                        name(std::string_view{storage.data(), storage.size()}))
                           .result == kue::CatalogReportResult::Recorded,
                   "every bounded entry is accepted after a failed retry");
    }
    run.expect(gCatalogs.commitEnemyCatalog().result == kue::CatalogCommitResult::Committed,
               "a corrected full-capacity retry commits");
    run.expect(gCatalogs.enemyCount() == kue::kRuntimeCatalogCapacity,
               "a full-capacity commit exposes all 256 entries");
    run.expect(gCatalogs.enemyAt(255).entry.name.size() == kue::kRuntimeCatalogNameCapacity,
               "the last full-capacity entry retains its complete name");
}

void testUnchangedIdentityAndAllocation(TestRun& run) {
    gCatalogs.resetForSessionEnd();
    commitInstalledCatalogs(run);
    const kue::EnemyCatalogGeneration generation = gCatalogs.enemyGeneration();
    const char* const nameAddress = gCatalogs.enemyAt(0).entry.name.data();

    gAllocations.store(0, std::memory_order_relaxed);
    gMeasureAllocations.store(true, std::memory_order_relaxed);
    bool operationsValid = true;
    for (std::size_t iteration = 0; iteration < 1000; ++iteration) {
        operationsValid =
            gCatalogs.beginEnemyCatalog() == kue::CatalogBeginResult::Begun && operationsValid;
        for (std::size_t index = 0; index < 33; ++index) {
            std::array<char, 32> storage{};
            const std::string_view value = indexedName(storage, {'e', index});
            operationsValid = gCatalogs.reportEnemy(asset(index + 1), name(value)).result ==
                                  kue::CatalogReportResult::Recorded &&
                              operationsValid;
        }
        operationsValid =
            gCatalogs.commitEnemyCatalog().result == kue::CatalogCommitResult::Unchanged &&
            operationsValid;
    }
    for (std::size_t iteration = 0; iteration < 100000; ++iteration) {
        operationsValid =
            gCatalogs.beginEnemyActivity(generation) == kue::EnemyActivityBeginResult::Begun &&
            operationsValid;
        const std::uint32_t count = static_cast<std::uint32_t>(iteration % 2U) + 1U;
        operationsValid =
            gCatalogs.reportEnemyActivity(gCatalogs.enemyAt(0).entry.id, count).result ==
                kue::EnemyActivityReportResult::Recorded &&
            operationsValid;
        operationsValid =
            gCatalogs.commitEnemyActivity().result == kue::EnemyActivityCommitResult::Changed &&
            operationsValid;
    }
    gMeasureAllocations.store(false, std::memory_order_relaxed);

    run.expect(gAllocations.load(std::memory_order_relaxed) == 0,
               "identity and activity transactions allocate no heap memory");
    run.expect(operationsValid, "every measured identity and activity operation succeeds");
    run.expect(gCatalogs.enemyGeneration() == generation,
               "identical identity commits do not churn generation");
    run.expect(gCatalogs.enemyAt(0).entry.name.data() == nameAddress,
               "activity-only updates never replace name storage");
    run.expect(gCatalogs.enemyAt(0).entry.name == "e-0",
               "activity-only updates never copy or alter name bytes");
    run.expect(gCatalogs.enemyAt(0).entry.activeCount == 2,
               "the final measured activity transaction publishes its exact count");
}

void testSessionReset(TestRun& run) {
    gCatalogs.resetForSessionEnd();
    commitInstalledCatalogs(run);
    const kue::EnemyTypeId priorId = gCatalogs.enemyAt(0).entry.id;
    const kue::ItemTypeId priorItemId = gCatalogs.itemAt(0).entry.id;
    run.expect(gCatalogs.beginEnemyActivity(gCatalogs.enemyGeneration()) ==
                   kue::EnemyActivityBeginResult::Begun,
               "a session-reset fixture begins an activity transaction");
    run.expect(gCatalogs.reportEnemyActivity(priorId, 5).result ==
                   kue::EnemyActivityReportResult::Recorded,
               "a session-reset fixture stages activity");
    gCatalogs.resetForSessionEnd();
    run.expect(gCatalogs.enemyCount() == 0 && gCatalogs.itemCount() == 0,
               "session reset clears both live catalog sizes");
    run.expect(gCatalogs.enemyGeneration().value == 0 && gCatalogs.itemGeneration().value == 0,
               "session reset invalidates both catalog generations");
    run.expect(gCatalogs.enemy(priorId).result == kue::CatalogLookupResult::NoCatalog,
               "session reset invalidates prior typed IDs");
    run.expect(gCatalogs.commitEnemyActivity().result ==
                   kue::EnemyActivityCommitResult::NoTransaction,
               "session reset discards an in-progress activity transaction");
    run.expect(gCatalogs.beginEnemyCatalog() == kue::CatalogBeginResult::Begun,
               "a new session can begin a fresh catalog transaction");
    run.expect(gCatalogs.reportEnemy(asset(9001), name("new enemy")).result ==
                   kue::CatalogReportResult::Recorded,
               "a new session stages a fresh enemy identity");
    run.expect(gCatalogs.commitEnemyCatalog().result == kue::CatalogCommitResult::Committed,
               "a new session commits a fresh enemy catalog");
    run.expect(gCatalogs.enemy(priorId).result == kue::CatalogLookupResult::StaleGeneration,
               "an old-session enemy ID cannot resolve after a new commit");
    run.expect(gCatalogs.enemyGeneration() != priorId.generation,
               "enemy generation never repeats across session reset");

    run.expect(gCatalogs.beginItemCatalog() == kue::CatalogBeginResult::Begun,
               "a new session can begin a fresh item transaction");
    run.expect(gCatalogs.reportItem(asset(9002), name("new item")).result ==
                   kue::CatalogReportResult::Recorded,
               "a new session stages a fresh item identity");
    run.expect(gCatalogs.commitItemCatalog().result == kue::CatalogCommitResult::Committed,
               "a new session commits a fresh item catalog");
    run.expect(gCatalogs.item(priorItemId).result == kue::CatalogLookupResult::StaleGeneration,
               "an old-session item ID cannot resolve after a new commit");
    run.expect(gCatalogs.itemGeneration() != priorItemId.generation,
               "item generation never repeats across session reset");
}

}

void* operator new(std::size_t size) {
    return allocate(size);
}

void* operator new[](std::size_t size) {
    return allocate(size);
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

void* operator new(std::size_t size, std::align_val_t alignment) {
    return allocateAligned({.size = size, .alignment = static_cast<std::size_t>(alignment)});
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
    return allocateAligned({.size = size, .alignment = static_cast<std::size_t>(alignment)});
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

int main() {
    TestRun run;
    testDeclaredStorageAndGeneration(run);
    testSignedRuntimeIdentityMapping(run);
    testActionResolutionRejectsStaleCatalogIds(run);
    testDenseTransactionsAndTypedLookup(run);
    testNameAndIdentityBoundaries(run);
    testFailurePreservesLiveCatalog(run);
    testInstalledWorkloadAndActivity(run);
    testFullCapacity(run);
    testUnchangedIdentityAndAllocation(run);
    testSessionReset(run);
    return run.result();
}
