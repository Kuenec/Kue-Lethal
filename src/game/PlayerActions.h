#ifndef KUE_GAME_PLAYER_ACTIONS_H
#define KUE_GAME_PLAYER_ACTIONS_H

#include "game/RuntimeCatalog.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <variant>

namespace kue {

enum class PlayerAction : std::uint8_t {
    None = 0,
    TeleportTo,
    Kill,
    Heal,
    KillAll,
    KillAllExceptLocal,
    LureAllEnemies,
    TeleportAllEnemies,
    TogglePersistentLure,
    SpawnEnemy,
    KillAllEnemies,
    StunAllEnemies,
    ToggleFly,
    ToggleShipHorn,
    ToggleShipLights,
    ToggleFactoryLights,
    BlowUpAllMines,
    ToggleAllMines,
    ToggleAllTurrets,
    BerserkAllTurrets,
    OpenShipDoorSpace,
    ForceTentacleAttack,
    SpawnMaskedEnemy,
    EjectEveryone,
    SpawnHoardingBugInfestation,
    ToggleMineshaftElevator,
    ToggleVehicleMagnet,
    ShootAllShotguns,
    ToggleShotgunSpam,
    ExplodeCruiser,
    SlideTaunt,
    ExplodeAllJetpacks,
    ToggleExplodeJetpacksOnGrab,
    ForceBridgeFall,
    ForceSmallBridgeFall,
    ToggleTerminalSound,
    ToggleDepositDeskSound,
    ToggleCarHorn,
    TogglePjManSpam,
    SpawnEnemyAtPlayer,
    SpawnItemAtPlayer,
    TeleportItemsToPlayer,
    SummonEnemyTypeAtPlayer,
    ClearInsanity,
    MaxInsanity,
    DepositShipScrap,
    AddTerminalCredits,
    ToggleThirdPerson,
};

enum class PlayerActionPayloadKind : std::uint8_t {
    Invalid,
    None,
    PlayerTarget,
    EnemySpawn,
    EnemyAtPlayer,
    ItemAtPlayer,
    PlushieInterval,
    TerminalCredits,
};

enum class EnemySpawnArea : std::uint8_t { Inside, Outside };

struct NoPlayerActionPayload {};

struct PlayerTargetPayload {
    std::uint64_t clientId;
};

struct EnemySpawnPayload {
    EnemyTypeId enemyType;
    std::uint8_t count;
    EnemySpawnArea area;
};

struct EnemyAtPlayerPayload {
    EnemyTypeId enemyType;
    std::uint8_t count;
    EnemySpawnArea area;
    std::uint64_t clientId;
};

struct ItemAtPlayerPayload {
    ItemTypeId itemType;
    std::uint8_t count;
    std::uint64_t clientId;
};

struct PlushieIntervalPayload {
    std::chrono::milliseconds interval;
};

inline constexpr int kMaximumTerminalCreditsPerAction = 1000000;

struct TerminalCreditsPayload {
    int amount;
};

using PlayerActionPayload =
    std::variant<NoPlayerActionPayload, PlayerTargetPayload, EnemySpawnPayload,
                 EnemyAtPlayerPayload, ItemAtPlayerPayload, PlushieIntervalPayload,
                 TerminalCreditsPayload>;

struct PlayerActionRequest {
    PlayerAction action = PlayerAction::None;
    PlayerActionPayload payload = NoPlayerActionPayload{};
};

enum class PlayerActionValidation : std::uint8_t {
    Valid,
    UnsupportedAction,
    PayloadMismatch,
    InvalidPayload,
};

enum class PlayerActionCatalogResolutionResult : std::uint8_t {
    NotRequired,
    Resolved,
    InvalidRequest,
    NoCatalog,
    StaleGeneration,
    IndexOutOfRange,
};

struct PlayerActionCatalogResolution {
    PlayerActionCatalogResolutionResult result =
        PlayerActionCatalogResolutionResult::InvalidRequest;
    std::uint16_t index = 0;
};

constexpr PlayerActionPayloadKind playerActionPayloadKind(PlayerAction action) noexcept {
    switch (action) {
    case PlayerAction::None:
        return PlayerActionPayloadKind::Invalid;
    case PlayerAction::TeleportTo:
    case PlayerAction::Kill:
    case PlayerAction::Heal:
    case PlayerAction::LureAllEnemies:
    case PlayerAction::TeleportAllEnemies:
    case PlayerAction::TogglePersistentLure:
    case PlayerAction::TeleportItemsToPlayer:
    case PlayerAction::ClearInsanity:
    case PlayerAction::MaxInsanity:
        return PlayerActionPayloadKind::PlayerTarget;
    case PlayerAction::SpawnEnemy:
        return PlayerActionPayloadKind::EnemySpawn;
    case PlayerAction::SpawnEnemyAtPlayer:
    case PlayerAction::SummonEnemyTypeAtPlayer:
        return PlayerActionPayloadKind::EnemyAtPlayer;
    case PlayerAction::SpawnItemAtPlayer:
        return PlayerActionPayloadKind::ItemAtPlayer;
    case PlayerAction::TogglePjManSpam:
        return PlayerActionPayloadKind::PlushieInterval;
    case PlayerAction::AddTerminalCredits:
        return PlayerActionPayloadKind::TerminalCredits;
    case PlayerAction::KillAll:
    case PlayerAction::KillAllExceptLocal:
    case PlayerAction::KillAllEnemies:
    case PlayerAction::StunAllEnemies:
    case PlayerAction::ToggleFly:
    case PlayerAction::ToggleShipHorn:
    case PlayerAction::ToggleShipLights:
    case PlayerAction::ToggleFactoryLights:
    case PlayerAction::BlowUpAllMines:
    case PlayerAction::ToggleAllMines:
    case PlayerAction::ToggleAllTurrets:
    case PlayerAction::BerserkAllTurrets:
    case PlayerAction::OpenShipDoorSpace:
    case PlayerAction::ForceTentacleAttack:
    case PlayerAction::SpawnMaskedEnemy:
    case PlayerAction::EjectEveryone:
    case PlayerAction::SpawnHoardingBugInfestation:
    case PlayerAction::ToggleMineshaftElevator:
    case PlayerAction::ToggleVehicleMagnet:
    case PlayerAction::ShootAllShotguns:
    case PlayerAction::ToggleShotgunSpam:
    case PlayerAction::ExplodeCruiser:
    case PlayerAction::SlideTaunt:
    case PlayerAction::ExplodeAllJetpacks:
    case PlayerAction::ToggleExplodeJetpacksOnGrab:
    case PlayerAction::ForceBridgeFall:
    case PlayerAction::ForceSmallBridgeFall:
    case PlayerAction::ToggleTerminalSound:
    case PlayerAction::ToggleDepositDeskSound:
    case PlayerAction::ToggleCarHorn:
    case PlayerAction::DepositShipScrap:
    case PlayerAction::ToggleThirdPerson:
        return PlayerActionPayloadKind::None;
    }
    return PlayerActionPayloadKind::Invalid;
}

constexpr PlayerActionPayloadKind
playerActionPayloadKind(const PlayerActionPayload& payload) noexcept {
    switch (payload.index()) {
    case 0:
        return PlayerActionPayloadKind::None;
    case 1:
        return PlayerActionPayloadKind::PlayerTarget;
    case 2:
        return PlayerActionPayloadKind::EnemySpawn;
    case 3:
        return PlayerActionPayloadKind::EnemyAtPlayer;
    case 4:
        return PlayerActionPayloadKind::ItemAtPlayer;
    case 5:
        return PlayerActionPayloadKind::PlushieInterval;
    case 6:
        return PlayerActionPayloadKind::TerminalCredits;
    default:
        return PlayerActionPayloadKind::Invalid;
    }
}

constexpr PlayerActionValidation
validatePlayerActionRequest(const PlayerActionRequest& request) noexcept {
    const PlayerActionPayloadKind required = playerActionPayloadKind(request.action);
    if (required == PlayerActionPayloadKind::Invalid)
        return PlayerActionValidation::UnsupportedAction;
    if (required != playerActionPayloadKind(request.payload))
        return PlayerActionValidation::PayloadMismatch;
    constexpr std::uint8_t maximumSpawnCount = 20;
    if (const auto* payload = std::get_if<EnemySpawnPayload>(&request.payload);
        payload && (payload->enemyType.generation.value == 0 ||
                    static_cast<std::size_t>(payload->enemyType.index) >= kRuntimeCatalogCapacity ||
                    payload->count == 0 || payload->count > maximumSpawnCount))
        return PlayerActionValidation::InvalidPayload;
    if (const auto* payload = std::get_if<EnemyAtPlayerPayload>(&request.payload);
        payload && (payload->enemyType.generation.value == 0 ||
                    static_cast<std::size_t>(payload->enemyType.index) >= kRuntimeCatalogCapacity ||
                    payload->count == 0 || payload->count > maximumSpawnCount))
        return PlayerActionValidation::InvalidPayload;
    if (const auto* payload = std::get_if<ItemAtPlayerPayload>(&request.payload);
        payload && (payload->itemType.generation.value == 0 ||
                    static_cast<std::size_t>(payload->itemType.index) >= kRuntimeCatalogCapacity ||
                    payload->count == 0 || payload->count > maximumSpawnCount))
        return PlayerActionValidation::InvalidPayload;
    if (const auto* payload = std::get_if<PlushieIntervalPayload>(&request.payload);
        payload && (payload->interval < std::chrono::milliseconds{0} ||
                    payload->interval > std::chrono::seconds{1}))
        return PlayerActionValidation::InvalidPayload;
    if (const auto* payload = std::get_if<TerminalCreditsPayload>(&request.payload);
        payload && (payload->amount <= 0 || payload->amount > kMaximumTerminalCreditsPerAction))
        return PlayerActionValidation::InvalidPayload;
    return PlayerActionValidation::Valid;
}

inline PlayerActionCatalogResolution
resolvePlayerActionCatalog(const PlayerActionRequest& request,
                           const RuntimeCatalogs& catalogs) noexcept {
    if (validatePlayerActionRequest(request) != PlayerActionValidation::Valid)
        return {};
    CatalogLookupResult lookupResult = CatalogLookupResult::Found;
    std::uint16_t index = 0;
    if (const auto* enemySpawn = std::get_if<EnemySpawnPayload>(&request.payload)) {
        const EnemyCatalogLookup lookup = catalogs.enemy(enemySpawn->enemyType);
        lookupResult = lookup.result;
        index = lookup.entry.id.index;
    } else if (const auto* enemyAtPlayer = std::get_if<EnemyAtPlayerPayload>(&request.payload)) {
        const EnemyCatalogLookup lookup = catalogs.enemy(enemyAtPlayer->enemyType);
        lookupResult = lookup.result;
        index = lookup.entry.id.index;
    } else if (const auto* itemAtPlayer = std::get_if<ItemAtPlayerPayload>(&request.payload)) {
        const ItemCatalogLookup lookup = catalogs.item(itemAtPlayer->itemType);
        lookupResult = lookup.result;
        index = lookup.entry.id.index;
    } else {
        return {PlayerActionCatalogResolutionResult::NotRequired, 0};
    }
    switch (lookupResult) {
    case CatalogLookupResult::Found:
        return {PlayerActionCatalogResolutionResult::Resolved, index};
    case CatalogLookupResult::NoCatalog:
        return {PlayerActionCatalogResolutionResult::NoCatalog, 0};
    case CatalogLookupResult::StaleGeneration:
        return {PlayerActionCatalogResolutionResult::StaleGeneration, 0};
    case CatalogLookupResult::IndexOutOfRange:
        return {PlayerActionCatalogResolutionResult::IndexOutOfRange, 0};
    }
    return {};
}

enum class PlayerActionEnqueueResult : std::uint8_t {
    Queued,
    InvalidRequest,
    CapacityExceeded,
};

enum class PlayerActionCaptureResult : std::uint8_t {
    Captured,
    InvalidRequest,
    Occupied,
};

class PendingPlayerAction final {
  public:
    [[nodiscard]] PlayerActionCaptureResult capture(const PlayerActionRequest& request) {
        if (validatePlayerActionRequest(request) != PlayerActionValidation::Valid)
            return PlayerActionCaptureResult::InvalidRequest;
        if (mRequest)
            return PlayerActionCaptureResult::Occupied;
        mRequest.emplace(request);
        return PlayerActionCaptureResult::Captured;
    }

    [[nodiscard]] bool read(PlayerActionRequest& output) const {
        if (!mRequest)
            return false;
        output = *mRequest;
        return true;
    }

    void applyEnqueueResult(PlayerActionEnqueueResult result) noexcept {
        switch (result) {
        case PlayerActionEnqueueResult::Queued:
        case PlayerActionEnqueueResult::InvalidRequest:
            mRequest.reset();
            return;
        case PlayerActionEnqueueResult::CapacityExceeded:
            return;
        }
    }

  private:
    std::optional<PlayerActionRequest> mRequest;
};

class PlayerActionQueue final {
  public:
    static constexpr std::size_t kCapacity = 64;

    PlayerActionEnqueueResult push(PlayerActionRequest request) {
        if (validatePlayerActionRequest(request) != PlayerActionValidation::Valid)
            return PlayerActionEnqueueResult::InvalidRequest;
        const std::lock_guard lock(mMutex);
        if (mCount == mEntries.size())
            return PlayerActionEnqueueResult::CapacityExceeded;
        mEntries[mTail] = request;
        mTail = (mTail + 1) % mEntries.size();
        ++mCount;
        return PlayerActionEnqueueResult::Queued;
    }

    bool pop(PlayerActionRequest& output) {
        const std::lock_guard lock(mMutex);
        if (mCount == 0)
            return false;
        output = mEntries[mHead];
        mHead = (mHead + 1) % mEntries.size();
        --mCount;
        return true;
    }

  private:
    std::array<PlayerActionRequest, kCapacity> mEntries{};
    std::size_t mHead = 0;
    std::size_t mTail = 0;
    std::size_t mCount = 0;
    std::mutex mMutex;
};

}

#endif
