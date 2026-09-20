#ifndef KUE_GAME_RUNTIME_CATALOG_H
#define KUE_GAME_RUNTIME_CATALOG_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kue {

inline constexpr std::size_t kRuntimeCatalogCapacity = 1024;
inline constexpr std::size_t kRuntimeCatalogNameCapacity = 256;
inline constexpr std::size_t kRuntimeCatalogTextCapacity =
    kRuntimeCatalogCapacity * kRuntimeCatalogNameCapacity;

struct RuntimeAssetIdentity {
    std::uint64_t value = 0;

    friend constexpr bool operator==(RuntimeAssetIdentity, RuntimeAssetIdentity) = default;
};

[[nodiscard]] constexpr RuntimeAssetIdentity
runtimeAssetIdentityFromSigned32(std::int32_t value) noexcept {
    if (value == 0)
        return {};
    return {static_cast<std::uint64_t>(static_cast<std::uint32_t>(value)) + 1U};
}

struct OrderedCatalogName {
    std::string_view utf8;
};

struct EnemyCatalogGeneration {
    std::uint64_t value = 0;

    friend constexpr bool operator==(EnemyCatalogGeneration, EnemyCatalogGeneration) = default;
};

struct ItemCatalogGeneration {
    std::uint64_t value = 0;

    friend constexpr bool operator==(ItemCatalogGeneration, ItemCatalogGeneration) = default;
};

struct MoonCatalogGeneration {
    std::uint64_t value = 0;

    friend constexpr bool operator==(MoonCatalogGeneration, MoonCatalogGeneration) = default;
};

struct EnemyTypeId {
    EnemyCatalogGeneration generation;
    std::uint16_t index = 0;

    friend constexpr bool operator==(EnemyTypeId, EnemyTypeId) = default;
};

struct ItemTypeId {
    ItemCatalogGeneration generation;
    std::uint16_t index = 0;

    friend constexpr bool operator==(ItemTypeId, ItemTypeId) = default;
};

struct MoonTypeId {
    MoonCatalogGeneration generation;
    std::uint16_t index = 0;

    friend constexpr bool operator==(MoonTypeId, MoonTypeId) = default;
};

enum class CatalogBeginResult : std::uint8_t { Begun, TransactionInProgress, GenerationExhausted };

enum class CatalogReportResult : std::uint8_t {
    None,
    Recorded,
    AlreadyRecorded,
    NoTransaction,
    WrongTransaction,
    PriorReportFailed,
    CapacityExceeded,
    InvalidAsset,
    EmptyName,
    EmbeddedNull,
    InvalidUtf8,
    NameTooLong,
    AssetNameConflict,
    DuplicateName
};

struct CatalogReportOutcome {
    CatalogReportResult result = CatalogReportResult::None;
    std::uint16_t entryIndex = 0;
    std::uint64_t actual = 0;
    std::uint64_t limit = 0;
};

enum class CatalogCommitResult : std::uint8_t {
    Committed,
    Unchanged,
    NoTransaction,
    WrongTransaction,
    PriorReportFailed,
    EmptyCatalog
};

struct CatalogCommitOutcome {
    CatalogCommitResult result = CatalogCommitResult::NoTransaction;
    CatalogReportOutcome failure;
};

enum class CatalogAbortResult : std::uint8_t { Aborted, NoTransaction, WrongTransaction };

enum class EnemyActivityBeginResult : std::uint8_t {
    Begun,
    TransactionInProgress,
    NoCatalog,
    StaleGeneration
};

enum class EnemyActivityReportResult : std::uint8_t {
    None,
    Recorded,
    NoTransaction,
    WrongTransaction,
    PriorReportFailed,
    StaleGeneration,
    IndexOutOfRange,
    ZeroCount,
    CountOverflow
};

struct EnemyActivityReportOutcome {
    EnemyActivityReportResult result = EnemyActivityReportResult::None;
    EnemyTypeId id;
    std::uint32_t additionalCount = 0;
};

enum class EnemyActivityCommitResult : std::uint8_t {
    Changed,
    Unchanged,
    NoTransaction,
    WrongTransaction,
    PriorReportFailed
};

struct EnemyActivityCommitOutcome {
    EnemyActivityCommitResult result = EnemyActivityCommitResult::NoTransaction;
    EnemyActivityReportOutcome failure;
};

enum class CatalogLookupResult : std::uint8_t {
    Found,
    NoCatalog,
    StaleGeneration,
    IndexOutOfRange
};

struct EnemyCatalogEntry {
    EnemyTypeId id;
    RuntimeAssetIdentity asset;
    std::string_view name;
    std::uint32_t activeCount = 0;
};

struct ItemCatalogEntry {
    ItemTypeId id;
    RuntimeAssetIdentity asset;
    std::string_view name;
};

struct MoonCatalogEntry {
    MoonTypeId id;
    RuntimeAssetIdentity asset;
    std::string_view name;
};

struct EnemyCatalogLookup {
    CatalogLookupResult result = CatalogLookupResult::NoCatalog;
    EnemyCatalogEntry entry;
};

struct ItemCatalogLookup {
    CatalogLookupResult result = CatalogLookupResult::NoCatalog;
    ItemCatalogEntry entry;
};

struct MoonCatalogLookup {
    CatalogLookupResult result = CatalogLookupResult::NoCatalog;
    MoonCatalogEntry entry;
};

class RuntimeCatalogs final {
  public:
    RuntimeCatalogs() = default;
    RuntimeCatalogs(const RuntimeCatalogs&) = delete;
    RuntimeCatalogs& operator=(const RuntimeCatalogs&) = delete;
    RuntimeCatalogs(RuntimeCatalogs&&) = delete;
    RuntimeCatalogs& operator=(RuntimeCatalogs&&) = delete;

    [[nodiscard]] CatalogBeginResult beginEnemyCatalog() noexcept;
    [[nodiscard]] CatalogReportOutcome reportEnemy(RuntimeAssetIdentity asset,
                                                   OrderedCatalogName name) noexcept;
    [[nodiscard]] CatalogCommitOutcome commitEnemyCatalog() noexcept;
    [[nodiscard]] CatalogAbortResult abortEnemyCatalog() noexcept;

    [[nodiscard]] CatalogBeginResult beginItemCatalog() noexcept;
    [[nodiscard]] CatalogReportOutcome reportItem(RuntimeAssetIdentity asset,
                                                  OrderedCatalogName name) noexcept;
    [[nodiscard]] CatalogCommitOutcome commitItemCatalog() noexcept;
    [[nodiscard]] CatalogAbortResult abortItemCatalog() noexcept;

    [[nodiscard]] CatalogBeginResult beginMoonCatalog() noexcept;
    [[nodiscard]] CatalogReportOutcome reportMoon(RuntimeAssetIdentity asset,
                                                  OrderedCatalogName name) noexcept;
    [[nodiscard]] CatalogCommitOutcome commitMoonCatalog() noexcept;
    [[nodiscard]] CatalogAbortResult abortMoonCatalog() noexcept;

    [[nodiscard]] EnemyActivityBeginResult
    beginEnemyActivity(EnemyCatalogGeneration generation) noexcept;
    [[nodiscard]] EnemyActivityReportOutcome reportEnemyActivity(EnemyTypeId id,
                                                                 std::uint32_t count) noexcept;
    [[nodiscard]] EnemyActivityCommitOutcome commitEnemyActivity() noexcept;
    [[nodiscard]] CatalogAbortResult abortEnemyActivity() noexcept;

    [[nodiscard]] std::size_t enemyCount() const noexcept;
    [[nodiscard]] std::size_t itemCount() const noexcept;
    [[nodiscard]] std::size_t moonCount() const noexcept;
    [[nodiscard]] EnemyCatalogGeneration enemyGeneration() const noexcept;
    [[nodiscard]] ItemCatalogGeneration itemGeneration() const noexcept;
    [[nodiscard]] MoonCatalogGeneration moonGeneration() const noexcept;
    [[nodiscard]] EnemyCatalogLookup enemyAt(std::size_t index) const noexcept;
    [[nodiscard]] ItemCatalogLookup itemAt(std::size_t index) const noexcept;
    [[nodiscard]] MoonCatalogLookup moonAt(std::size_t index) const noexcept;
    [[nodiscard]] EnemyCatalogLookup enemy(EnemyTypeId id) const noexcept;
    [[nodiscard]] ItemCatalogLookup item(ItemTypeId id) const noexcept;
    [[nodiscard]] MoonCatalogLookup moon(MoonTypeId id) const noexcept;

    void resetForSessionEnd() noexcept;

  private:
    struct StoredCatalogEntry {
        RuntimeAssetIdentity asset;
        std::uint64_t nameHash = 0;
        std::uint32_t nameOffset = 0;
        std::uint16_t nameLength = 0;
    };

    struct CatalogStorage {
        std::array<StoredCatalogEntry, kRuntimeCatalogCapacity> entries{};
        std::array<char, kRuntimeCatalogTextCapacity> text{};
        std::uint64_t generation = 0;
        std::uint32_t textBytes = 0;
        std::uint16_t count = 0;
    };

    struct CatalogStaging {
        std::array<StoredCatalogEntry, kRuntimeCatalogCapacity> entries{};
        std::array<char, kRuntimeCatalogTextCapacity> text{};
        std::uint32_t textBytes = 0;
        std::uint16_t count = 0;
    };

    enum class Transaction : std::uint8_t {
        None,
        EnemyCatalog,
        ItemCatalog,
        MoonCatalog,
        EnemyActivity
    };

    [[nodiscard]] CatalogBeginResult beginCatalog(Transaction transaction,
                                                  std::uint64_t generation) noexcept;
    [[nodiscard]] CatalogReportOutcome reportCatalog(Transaction transaction,
                                                     RuntimeAssetIdentity asset,
                                                     OrderedCatalogName name) noexcept;
    [[nodiscard]] CatalogCommitOutcome commitCatalog(Transaction transaction,
                                                     CatalogStorage& live) noexcept;
    [[nodiscard]] CatalogAbortResult abort(Transaction transaction) noexcept;
    [[nodiscard]] std::string_view stagingName(std::size_t index) const noexcept;
    [[nodiscard]] static std::string_view liveName(const CatalogStorage& catalog,
                                                   std::size_t index) noexcept;
    [[nodiscard]] static std::uint64_t nameHash(std::string_view name) noexcept;
    [[nodiscard]] static bool sameCatalog(const CatalogStorage& live,
                                          const CatalogStaging& staging) noexcept;
    void failCatalog(CatalogReportOutcome failure) noexcept;
    void clearTransaction() noexcept;

    CatalogStorage mEnemies;
    CatalogStorage mItems;
    CatalogStorage mMoons;
    CatalogStaging mStaging;
    std::array<std::uint32_t, kRuntimeCatalogCapacity> mEnemyActivity{};
    std::array<std::uint32_t, kRuntimeCatalogCapacity> mStagedEnemyActivity{};
    CatalogReportOutcome mCatalogFailure;
    EnemyActivityReportOutcome mActivityFailure;
    EnemyCatalogGeneration mActivityGeneration;
    Transaction mTransaction = Transaction::None;
};

}

#endif
