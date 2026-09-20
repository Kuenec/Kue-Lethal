#include "game/RuntimeCatalog.h"

#include "core/Utf8.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace kue {
namespace {

constexpr std::uint64_t kNameHashOffset = 1469598103934665603ULL;
constexpr std::uint64_t kNameHashPrime = 1099511628211ULL;

struct CatalogReportEvidence {
    std::size_t entryIndex;
    std::uint64_t actual;
    std::uint64_t limit;
};

CatalogReportOutcome catalogReport(CatalogReportResult result,
                                   CatalogReportEvidence evidence) noexcept {
    return {.result = result,
            .entryIndex = static_cast<std::uint16_t>(evidence.entryIndex),
            .actual = evidence.actual,
            .limit = evidence.limit};
}

EnemyActivityReportOutcome activityReport(EnemyActivityReportResult result, EnemyTypeId id,
                                          std::uint32_t count) noexcept {
    return {result, id, count};
}

}

CatalogBeginResult RuntimeCatalogs::beginEnemyCatalog() noexcept {
    return beginCatalog(Transaction::EnemyCatalog, mEnemies.generation);
}

CatalogReportOutcome RuntimeCatalogs::reportEnemy(RuntimeAssetIdentity runtimeAsset,
                                                  OrderedCatalogName orderedName) noexcept {
    return reportCatalog(Transaction::EnemyCatalog, runtimeAsset, orderedName);
}

CatalogCommitOutcome RuntimeCatalogs::commitEnemyCatalog() noexcept {
    return commitCatalog(Transaction::EnemyCatalog, mEnemies);
}

CatalogAbortResult RuntimeCatalogs::abortEnemyCatalog() noexcept {
    return abort(Transaction::EnemyCatalog);
}

CatalogBeginResult RuntimeCatalogs::beginItemCatalog() noexcept {
    return beginCatalog(Transaction::ItemCatalog, mItems.generation);
}

CatalogReportOutcome RuntimeCatalogs::reportItem(RuntimeAssetIdentity runtimeAsset,
                                                 OrderedCatalogName orderedName) noexcept {
    return reportCatalog(Transaction::ItemCatalog, runtimeAsset, orderedName);
}

CatalogCommitOutcome RuntimeCatalogs::commitItemCatalog() noexcept {
    return commitCatalog(Transaction::ItemCatalog, mItems);
}

CatalogAbortResult RuntimeCatalogs::abortItemCatalog() noexcept {
    return abort(Transaction::ItemCatalog);
}

CatalogBeginResult RuntimeCatalogs::beginMoonCatalog() noexcept {
    return beginCatalog(Transaction::MoonCatalog, mMoons.generation);
}

CatalogReportOutcome RuntimeCatalogs::reportMoon(RuntimeAssetIdentity runtimeAsset,
                                                 OrderedCatalogName orderedName) noexcept {
    return reportCatalog(Transaction::MoonCatalog, runtimeAsset, orderedName);
}

CatalogCommitOutcome RuntimeCatalogs::commitMoonCatalog() noexcept {
    return commitCatalog(Transaction::MoonCatalog, mMoons);
}

CatalogAbortResult RuntimeCatalogs::abortMoonCatalog() noexcept {
    return abort(Transaction::MoonCatalog);
}

CatalogBeginResult RuntimeCatalogs::beginCatalog(Transaction transaction,
                                                 std::uint64_t generation) noexcept {
    if (mTransaction != Transaction::None)
        return CatalogBeginResult::TransactionInProgress;
    if (generation == std::numeric_limits<std::uint64_t>::max())
        return CatalogBeginResult::GenerationExhausted;
    mTransaction = transaction;
    mStaging.count = 0;
    mStaging.textBytes = 0;
    mCatalogFailure = {};
    return CatalogBeginResult::Begun;
}

CatalogReportOutcome RuntimeCatalogs::reportCatalog(Transaction transaction,
                                                    RuntimeAssetIdentity runtimeAsset,
                                                    OrderedCatalogName orderedName) noexcept {
    if (mTransaction == Transaction::None)
        return catalogReport(CatalogReportResult::NoTransaction,
                             {.entryIndex = 0, .actual = 0, .limit = 0});
    if (mTransaction != transaction)
        return catalogReport(CatalogReportResult::WrongTransaction,
                             {.entryIndex = 0, .actual = 0, .limit = 0});
    if (mCatalogFailure.result != CatalogReportResult::None) {
        CatalogReportOutcome prior = mCatalogFailure;
        prior.result = CatalogReportResult::PriorReportFailed;
        return prior;
    }

    const std::size_t entryIndex = mStaging.count;
    if (entryIndex == kRuntimeCatalogCapacity) {
        const CatalogReportOutcome failure =
            catalogReport(CatalogReportResult::CapacityExceeded,
                          {.entryIndex = entryIndex,
                           .actual = static_cast<std::uint64_t>(entryIndex + 1),
                           .limit = static_cast<std::uint64_t>(kRuntimeCatalogCapacity)});
        failCatalog(failure);
        return failure;
    }
    if (runtimeAsset.value == 0) {
        const CatalogReportOutcome failure = catalogReport(
            CatalogReportResult::InvalidAsset, {.entryIndex = entryIndex, .actual = 0, .limit = 1});
        failCatalog(failure);
        return failure;
    }

    const std::size_t nameBytes = orderedName.utf8.size();
    if (nameBytes == 0) {
        const CatalogReportOutcome failure = catalogReport(
            CatalogReportResult::EmptyName, {.entryIndex = entryIndex, .actual = 0, .limit = 1});
        failCatalog(failure);
        return failure;
    }
    if (nameBytes > kRuntimeCatalogNameCapacity) {
        const CatalogReportOutcome failure =
            catalogReport(CatalogReportResult::NameTooLong,
                          {.entryIndex = entryIndex,
                           .actual = static_cast<std::uint64_t>(nameBytes),
                           .limit = static_cast<std::uint64_t>(kRuntimeCatalogNameCapacity)});
        failCatalog(failure);
        return failure;
    }
    if (std::memchr(orderedName.utf8.data(), 0, nameBytes)) {
        const CatalogReportOutcome failure = catalogReport(
            CatalogReportResult::EmbeddedNull, {.entryIndex = entryIndex,
                                                .actual = static_cast<std::uint64_t>(nameBytes),
                                                .limit = 0});
        failCatalog(failure);
        return failure;
    }
    if (!isValidUtf8(orderedName.utf8)) {
        const CatalogReportOutcome failure = catalogReport(
            CatalogReportResult::InvalidUtf8, {.entryIndex = entryIndex,
                                               .actual = static_cast<std::uint64_t>(nameBytes),
                                               .limit = 0});
        failCatalog(failure);
        return failure;
    }

    const std::uint64_t hash = nameHash(orderedName.utf8);
    for (std::size_t index = 0; index < entryIndex; ++index) {
        const StoredCatalogEntry& existing = mStaging.entries[index];
        const bool sameName = existing.nameHash == hash && existing.nameLength == nameBytes &&
                              stagingName(index) == orderedName.utf8;
        if (existing.asset == runtimeAsset) {
            if (sameName)
                return catalogReport(CatalogReportResult::AlreadyRecorded,
                                     {.entryIndex = index, .actual = 0, .limit = 0});
            const CatalogReportOutcome failure = catalogReport(
                CatalogReportResult::AssetNameConflict, {.entryIndex = entryIndex,
                                                         .actual = runtimeAsset.value,
                                                         .limit = existing.asset.value});
            failCatalog(failure);
            return failure;
        }
        if (sameName) {
            const CatalogReportOutcome failure =
                catalogReport(CatalogReportResult::DuplicateName, {.entryIndex = entryIndex,
                                                                   .actual = runtimeAsset.value,
                                                                   .limit = existing.asset.value});
            failCatalog(failure);
            return failure;
        }
    }

    const std::size_t textBytes = mStaging.textBytes;
    std::memcpy(mStaging.text.data() + textBytes, orderedName.utf8.data(), nameBytes);
    mStaging.entries[entryIndex] = {runtimeAsset, hash, static_cast<std::uint32_t>(textBytes),
                                    static_cast<std::uint16_t>(nameBytes)};
    mStaging.textBytes = static_cast<std::uint32_t>(textBytes + nameBytes);
    mStaging.count = static_cast<std::uint16_t>(entryIndex + 1);
    return catalogReport(CatalogReportResult::Recorded,
                         {.entryIndex = entryIndex, .actual = 0, .limit = 0});
}

CatalogCommitOutcome RuntimeCatalogs::commitCatalog(Transaction transaction,
                                                    CatalogStorage& live) noexcept {
    if (mTransaction == Transaction::None)
        return {CatalogCommitResult::NoTransaction, {}};
    if (mTransaction != transaction)
        return {CatalogCommitResult::WrongTransaction, {}};
    if (mCatalogFailure.result != CatalogReportResult::None) {
        const CatalogReportOutcome failure = mCatalogFailure;
        clearTransaction();
        return {CatalogCommitResult::PriorReportFailed, failure};
    }
    if (mStaging.count == 0) {
        clearTransaction();
        return {CatalogCommitResult::EmptyCatalog, {}};
    }

    if (sameCatalog(live, mStaging)) {
        clearTransaction();
        return {CatalogCommitResult::Unchanged, {}};
    }

    const std::size_t entryCount = mStaging.count;
    const std::size_t textBytes = mStaging.textBytes;
    std::copy_n(mStaging.entries.begin(), entryCount, live.entries.begin());
    std::memcpy(live.text.data(), mStaging.text.data(), textBytes);
    live.count = mStaging.count;
    live.textBytes = mStaging.textBytes;
    ++live.generation;
    if (transaction == Transaction::EnemyCatalog) {
        mEnemyActivity.fill(0);
    }
    clearTransaction();
    return {CatalogCommitResult::Committed, {}};
}

CatalogAbortResult RuntimeCatalogs::abort(Transaction transaction) noexcept {
    if (mTransaction == Transaction::None)
        return CatalogAbortResult::NoTransaction;
    if (mTransaction != transaction)
        return CatalogAbortResult::WrongTransaction;
    clearTransaction();
    return CatalogAbortResult::Aborted;
}

EnemyActivityBeginResult
RuntimeCatalogs::beginEnemyActivity(EnemyCatalogGeneration generation) noexcept {
    if (mTransaction != Transaction::None)
        return EnemyActivityBeginResult::TransactionInProgress;
    if (mEnemies.count == 0)
        return EnemyActivityBeginResult::NoCatalog;
    if (generation.value != mEnemies.generation)
        return EnemyActivityBeginResult::StaleGeneration;
    mTransaction = Transaction::EnemyActivity;
    mActivityGeneration = generation;
    mActivityFailure = {};
    std::fill_n(mStagedEnemyActivity.begin(), static_cast<std::size_t>(mEnemies.count), 0);
    return EnemyActivityBeginResult::Begun;
}

EnemyActivityReportOutcome RuntimeCatalogs::reportEnemyActivity(EnemyTypeId id,
                                                                std::uint32_t count) noexcept {
    if (mTransaction == Transaction::None)
        return activityReport(EnemyActivityReportResult::NoTransaction, id, count);
    if (mTransaction != Transaction::EnemyActivity)
        return activityReport(EnemyActivityReportResult::WrongTransaction, id, count);
    if (mActivityFailure.result != EnemyActivityReportResult::None) {
        EnemyActivityReportOutcome prior = mActivityFailure;
        prior.result = EnemyActivityReportResult::PriorReportFailed;
        return prior;
    }

    EnemyActivityReportOutcome failure;
    if (id.generation != mActivityGeneration) {
        failure = activityReport(EnemyActivityReportResult::StaleGeneration, id, count);
    } else if (static_cast<std::size_t>(id.index) >= static_cast<std::size_t>(mEnemies.count)) {
        failure = activityReport(EnemyActivityReportResult::IndexOutOfRange, id, count);
    } else if (count == 0) {
        failure = activityReport(EnemyActivityReportResult::ZeroCount, id, count);
    } else {
        const std::size_t index = id.index;
        const std::uint32_t current = mStagedEnemyActivity[index];
        if (count >= std::numeric_limits<std::uint32_t>::max() - current) {
            failure = activityReport(EnemyActivityReportResult::CountOverflow, id, count);
        } else {
            mStagedEnemyActivity[index] = current + count;
            return activityReport(EnemyActivityReportResult::Recorded, id, count);
        }
    }
    mActivityFailure = failure;
    return failure;
}

EnemyActivityCommitOutcome RuntimeCatalogs::commitEnemyActivity() noexcept {
    if (mTransaction == Transaction::None)
        return {EnemyActivityCommitResult::NoTransaction, {}};
    if (mTransaction != Transaction::EnemyActivity)
        return {EnemyActivityCommitResult::WrongTransaction, {}};
    if (mActivityFailure.result != EnemyActivityReportResult::None) {
        const EnemyActivityReportOutcome failure = mActivityFailure;
        clearTransaction();
        return {EnemyActivityCommitResult::PriorReportFailed, failure};
    }

    const std::size_t enemyCount = mEnemies.count;
    const bool unchanged =
        std::equal(mStagedEnemyActivity.begin(), mStagedEnemyActivity.begin() + enemyCount,
                   mEnemyActivity.begin());
    if (unchanged) {
        clearTransaction();
        return {EnemyActivityCommitResult::Unchanged, {}};
    }
    std::copy_n(mStagedEnemyActivity.begin(), enemyCount, mEnemyActivity.begin());
    clearTransaction();
    return {EnemyActivityCommitResult::Changed, {}};
}

CatalogAbortResult RuntimeCatalogs::abortEnemyActivity() noexcept {
    return abort(Transaction::EnemyActivity);
}

std::size_t RuntimeCatalogs::enemyCount() const noexcept {
    return mEnemies.count;
}

std::size_t RuntimeCatalogs::itemCount() const noexcept {
    return mItems.count;
}

std::size_t RuntimeCatalogs::moonCount() const noexcept {
    return mMoons.count;
}

EnemyCatalogGeneration RuntimeCatalogs::enemyGeneration() const noexcept {
    return {mEnemies.count == 0 ? 0 : mEnemies.generation};
}

ItemCatalogGeneration RuntimeCatalogs::itemGeneration() const noexcept {
    return {mItems.count == 0 ? 0 : mItems.generation};
}

MoonCatalogGeneration RuntimeCatalogs::moonGeneration() const noexcept {
    return {mMoons.count == 0 ? 0 : mMoons.generation};
}

EnemyCatalogLookup RuntimeCatalogs::enemyAt(std::size_t index) const noexcept {
    if (mEnemies.count == 0)
        return {CatalogLookupResult::NoCatalog, {}};
    if (index >= static_cast<std::size_t>(mEnemies.count))
        return {CatalogLookupResult::IndexOutOfRange, {}};
    const StoredCatalogEntry& stored = mEnemies.entries[index];
    return {CatalogLookupResult::Found,
            {EnemyTypeId{{mEnemies.generation}, static_cast<std::uint16_t>(index)}, stored.asset,
             liveName(mEnemies, index), mEnemyActivity[index]}};
}

ItemCatalogLookup RuntimeCatalogs::itemAt(std::size_t index) const noexcept {
    if (mItems.count == 0)
        return {CatalogLookupResult::NoCatalog, {}};
    if (index >= static_cast<std::size_t>(mItems.count))
        return {CatalogLookupResult::IndexOutOfRange, {}};
    const StoredCatalogEntry& stored = mItems.entries[index];
    return {CatalogLookupResult::Found,
            {ItemTypeId{{mItems.generation}, static_cast<std::uint16_t>(index)}, stored.asset,
             liveName(mItems, index)}};
}

MoonCatalogLookup RuntimeCatalogs::moonAt(std::size_t index) const noexcept {
    if (mMoons.count == 0)
        return {CatalogLookupResult::NoCatalog, {}};
    if (index >= static_cast<std::size_t>(mMoons.count))
        return {CatalogLookupResult::IndexOutOfRange, {}};
    const StoredCatalogEntry& stored = mMoons.entries[index];
    return {CatalogLookupResult::Found,
            {MoonTypeId{{mMoons.generation}, static_cast<std::uint16_t>(index)}, stored.asset,
             liveName(mMoons, index)}};
}

EnemyCatalogLookup RuntimeCatalogs::enemy(EnemyTypeId id) const noexcept {
    if (mEnemies.count == 0)
        return {CatalogLookupResult::NoCatalog, {}};
    if (id.generation.value != mEnemies.generation)
        return {CatalogLookupResult::StaleGeneration, {}};
    return enemyAt(id.index);
}

ItemCatalogLookup RuntimeCatalogs::item(ItemTypeId id) const noexcept {
    if (mItems.count == 0)
        return {CatalogLookupResult::NoCatalog, {}};
    if (id.generation.value != mItems.generation)
        return {CatalogLookupResult::StaleGeneration, {}};
    return itemAt(id.index);
}

MoonCatalogLookup RuntimeCatalogs::moon(MoonTypeId id) const noexcept {
    if (mMoons.count == 0)
        return {CatalogLookupResult::NoCatalog, {}};
    if (id.generation.value != mMoons.generation)
        return {CatalogLookupResult::StaleGeneration, {}};
    return moonAt(id.index);
}

void RuntimeCatalogs::resetForSessionEnd() noexcept {
    mEnemies.entries.fill(StoredCatalogEntry{});
    mEnemies.text.fill(0);
    mEnemies.textBytes = 0;
    mEnemies.count = 0;
    mItems.entries.fill(StoredCatalogEntry{});
    mItems.text.fill(0);
    mItems.textBytes = 0;
    mItems.count = 0;
    mMoons.entries.fill(StoredCatalogEntry{});
    mMoons.text.fill(0);
    mMoons.textBytes = 0;
    mMoons.count = 0;
    mStaging.entries.fill(StoredCatalogEntry{});
    mStaging.text.fill(0);
    mStaging.textBytes = 0;
    mStaging.count = 0;
    mEnemyActivity.fill(0);
    mStagedEnemyActivity.fill(0);
    mCatalogFailure = {};
    mActivityFailure = {};
    mActivityGeneration = {};
    mTransaction = Transaction::None;
}

std::string_view RuntimeCatalogs::stagingName(std::size_t index) const noexcept {
    const StoredCatalogEntry& entry = mStaging.entries[index];
    return {mStaging.text.data() + entry.nameOffset, entry.nameLength};
}

std::string_view RuntimeCatalogs::liveName(const CatalogStorage& catalog,
                                           std::size_t index) noexcept {
    const StoredCatalogEntry& entry = catalog.entries[index];
    return {catalog.text.data() + entry.nameOffset, entry.nameLength};
}

std::uint64_t RuntimeCatalogs::nameHash(std::string_view value) noexcept {
    std::uint64_t hash = kNameHashOffset;
    for (const char character : value) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(character));
        hash *= kNameHashPrime;
    }
    return hash;
}

bool RuntimeCatalogs::sameCatalog(const CatalogStorage& live,
                                  const CatalogStaging& staging) noexcept {
    if (live.count != staging.count || live.textBytes != staging.textBytes)
        return false;
    const std::size_t entryCount = staging.count;
    for (std::size_t index = 0; index < entryCount; ++index) {
        const StoredCatalogEntry& left = live.entries[index];
        const StoredCatalogEntry& right = staging.entries[index];
        if (left.asset != right.asset || left.nameHash != right.nameHash ||
            left.nameOffset != right.nameOffset || left.nameLength != right.nameLength) {
            return false;
        }
    }
    return std::memcmp(live.text.data(), staging.text.data(), staging.textBytes) == 0;
}

void RuntimeCatalogs::failCatalog(CatalogReportOutcome failure) noexcept {
    mCatalogFailure = failure;
}

void RuntimeCatalogs::clearTransaction() noexcept {
    mTransaction = Transaction::None;
}

}
