#include "game/PlayerActions.h"
#include "game/RuntimeCatalog.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>
#include <type_traits>
#include <variant>

namespace {

class TestRun final {
  public:
    void expect(bool condition, std::string_view subject, std::string_view contract) {
        ++mAssertions;
        if (condition)
            return;
        ++mFailures;
        std::cerr << "FAIL: " << subject << ": " << contract << '\n';
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

struct ActionContract {
    kue::PlayerAction action;
    kue::PlayerActionPayloadKind payloadKind;
    std::string_view name;
};

constexpr std::array<ActionContract, 44> kActionContracts{{
    {kue::PlayerAction::TeleportTo, kue::PlayerActionPayloadKind::PlayerTarget, "TeleportTo"},
    {kue::PlayerAction::Kill, kue::PlayerActionPayloadKind::PlayerTarget, "Kill"},
    {kue::PlayerAction::Heal, kue::PlayerActionPayloadKind::PlayerTarget, "Heal"},
    {kue::PlayerAction::KillAll, kue::PlayerActionPayloadKind::None, "KillAll"},
    {kue::PlayerAction::KillAllExceptLocal, kue::PlayerActionPayloadKind::None,
     "KillAllExceptLocal"},
    {kue::PlayerAction::LureAllEnemies, kue::PlayerActionPayloadKind::PlayerTarget,
     "LureAllEnemies"},
    {kue::PlayerAction::TeleportAllEnemies, kue::PlayerActionPayloadKind::PlayerTarget,
     "TeleportAllEnemies"},
    {kue::PlayerAction::TogglePersistentLure, kue::PlayerActionPayloadKind::PlayerTarget,
     "TogglePersistentLure"},
    {kue::PlayerAction::SpawnEnemy, kue::PlayerActionPayloadKind::EnemySpawn, "SpawnEnemy"},
    {kue::PlayerAction::KillAllEnemies, kue::PlayerActionPayloadKind::None, "KillAllEnemies"},
    {kue::PlayerAction::StunAllEnemies, kue::PlayerActionPayloadKind::None, "StunAllEnemies"},
    {kue::PlayerAction::ToggleFly, kue::PlayerActionPayloadKind::None, "ToggleFly"},
    {kue::PlayerAction::ToggleShipHorn, kue::PlayerActionPayloadKind::None, "ToggleShipHorn"},
    {kue::PlayerAction::ToggleShipLights, kue::PlayerActionPayloadKind::None, "ToggleShipLights"},
    {kue::PlayerAction::ToggleFactoryLights, kue::PlayerActionPayloadKind::None,
     "ToggleFactoryLights"},
    {kue::PlayerAction::BlowUpAllMines, kue::PlayerActionPayloadKind::None, "BlowUpAllMines"},
    {kue::PlayerAction::ToggleAllMines, kue::PlayerActionPayloadKind::None, "ToggleAllMines"},
    {kue::PlayerAction::ToggleAllTurrets, kue::PlayerActionPayloadKind::None, "ToggleAllTurrets"},
    {kue::PlayerAction::BerserkAllTurrets, kue::PlayerActionPayloadKind::None, "BerserkAllTurrets"},
    {kue::PlayerAction::OpenShipDoorSpace, kue::PlayerActionPayloadKind::None, "OpenShipDoorSpace"},
    {kue::PlayerAction::ForceTentacleAttack, kue::PlayerActionPayloadKind::None,
     "ForceTentacleAttack"},
    {kue::PlayerAction::SpawnMaskedEnemy, kue::PlayerActionPayloadKind::None, "SpawnMaskedEnemy"},
    {kue::PlayerAction::EjectEveryone, kue::PlayerActionPayloadKind::None, "EjectEveryone"},
    {kue::PlayerAction::SpawnHoardingBugInfestation, kue::PlayerActionPayloadKind::None,
     "SpawnHoardingBugInfestation"},
    {kue::PlayerAction::ToggleMineshaftElevator, kue::PlayerActionPayloadKind::None,
     "ToggleMineshaftElevator"},
    {kue::PlayerAction::ToggleVehicleMagnet, kue::PlayerActionPayloadKind::None,
     "ToggleVehicleMagnet"},
    {kue::PlayerAction::ShootAllShotguns, kue::PlayerActionPayloadKind::None, "ShootAllShotguns"},
    {kue::PlayerAction::ToggleShotgunSpam, kue::PlayerActionPayloadKind::None, "ToggleShotgunSpam"},
    {kue::PlayerAction::ExplodeCruiser, kue::PlayerActionPayloadKind::None, "ExplodeCruiser"},
    {kue::PlayerAction::SlideTaunt, kue::PlayerActionPayloadKind::None, "SlideTaunt"},
    {kue::PlayerAction::ExplodeAllJetpacks, kue::PlayerActionPayloadKind::None,
     "ExplodeAllJetpacks"},
    {kue::PlayerAction::ToggleExplodeJetpacksOnGrab, kue::PlayerActionPayloadKind::None,
     "ToggleExplodeJetpacksOnGrab"},
    {kue::PlayerAction::ForceBridgeFall, kue::PlayerActionPayloadKind::None, "ForceBridgeFall"},
    {kue::PlayerAction::ForceSmallBridgeFall, kue::PlayerActionPayloadKind::None,
     "ForceSmallBridgeFall"},
    {kue::PlayerAction::ToggleTerminalSound, kue::PlayerActionPayloadKind::None,
     "ToggleTerminalSound"},
    {kue::PlayerAction::ToggleDepositDeskSound, kue::PlayerActionPayloadKind::None,
     "ToggleDepositDeskSound"},
    {kue::PlayerAction::ToggleCarHorn, kue::PlayerActionPayloadKind::None, "ToggleCarHorn"},
    {kue::PlayerAction::TogglePjManSpam, kue::PlayerActionPayloadKind::PlushieInterval,
     "TogglePjManSpam"},
    {kue::PlayerAction::SpawnEnemyAtPlayer, kue::PlayerActionPayloadKind::EnemyAtPlayer,
     "SpawnEnemyAtPlayer"},
    {kue::PlayerAction::SpawnItemAtPlayer, kue::PlayerActionPayloadKind::ItemAtPlayer,
     "SpawnItemAtPlayer"},
    {kue::PlayerAction::TeleportItemsToPlayer, kue::PlayerActionPayloadKind::PlayerTarget,
     "TeleportItemsToPlayer"},
    {kue::PlayerAction::SummonEnemyTypeAtPlayer, kue::PlayerActionPayloadKind::EnemyAtPlayer,
     "SummonEnemyTypeAtPlayer"},
    {kue::PlayerAction::ClearInsanity, kue::PlayerActionPayloadKind::PlayerTarget, "ClearInsanity"},
    {kue::PlayerAction::MaxInsanity, kue::PlayerActionPayloadKind::PlayerTarget, "MaxInsanity"},
}};

constexpr std::array<kue::PlayerActionPayloadKind, 6> kPayloadKinds{{
    kue::PlayerActionPayloadKind::None,
    kue::PlayerActionPayloadKind::PlayerTarget,
    kue::PlayerActionPayloadKind::EnemySpawn,
    kue::PlayerActionPayloadKind::EnemyAtPlayer,
    kue::PlayerActionPayloadKind::ItemAtPlayer,
    kue::PlayerActionPayloadKind::PlushieInterval,
}};

constexpr kue::EnemyTypeId kEnemyType{{7U}, 3U};
constexpr kue::ItemTypeId kItemType{{11U}, 3U};

std::array<kue::PlayerActionPayload, kPayloadKinds.size()> payloadFixtures() {
    return {{
        kue::NoPlayerActionPayload{},
        kue::PlayerTargetPayload{17U},
        kue::EnemySpawnPayload{kEnemyType, 2U, kue::EnemySpawnArea::Inside},
        kue::EnemyAtPlayerPayload{kEnemyType, 2U, kue::EnemySpawnArea::Outside, 17U},
        kue::ItemAtPlayerPayload{kItemType, 2U, 17U},
        kue::PlushieIntervalPayload{std::chrono::milliseconds{250}},
    }};
}

void testActionPayloadContracts(TestRun& run) {
    const std::array<kue::PlayerActionPayload, kPayloadKinds.size()> payloads = payloadFixtures();
    for (std::size_t index = 0; index < kActionContracts.size(); ++index) {
        const ActionContract& contract = kActionContracts[index];
        run.expect(static_cast<std::uint8_t>(contract.action) == index + 1U, contract.name,
                   "stable production action value changed");
        run.expect(kue::playerActionPayloadKind(contract.action) == contract.payloadKind,
                   contract.name, "payload kind differs from the production action contract");
        std::size_t acceptedPayloads = 0;
        for (std::size_t payloadIndex = 0; payloadIndex < payloads.size(); ++payloadIndex) {
            const kue::PlayerActionRequest request{contract.action, payloads[payloadIndex]};
            const kue::PlayerActionValidation actual = kue::validatePlayerActionRequest(request);
            const kue::PlayerActionValidation expected =
                kPayloadKinds[payloadIndex] == contract.payloadKind
                    ? kue::PlayerActionValidation::Valid
                    : kue::PlayerActionValidation::PayloadMismatch;
            run.expect(actual == expected, contract.name,
                       "typed payload acceptance differs from the production contract");
            if (actual == kue::PlayerActionValidation::Valid)
                ++acceptedPayloads;
        }
        run.expect(acceptedPayloads == 1U, contract.name,
                   "an action must accept exactly one payload type");
    }

    run.expect(kue::playerActionPayloadKind(kue::PlayerAction::None) ==
                   kue::PlayerActionPayloadKind::Invalid,
               "None", "the empty action must not have a payload kind");
    run.expect(
        kue::validatePlayerActionRequest({kue::PlayerAction::None, kue::NoPlayerActionPayload{}}) ==
            kue::PlayerActionValidation::UnsupportedAction,
        "None", "the empty action must be rejected");
}

void testPayloadBoundaries(TestRun& run) {
    using enum kue::PlayerActionValidation;
    run.expect(kue::validatePlayerActionRequest(
                   {kue::PlayerAction::SpawnEnemy,
                    kue::EnemySpawnPayload{{{}, 0U}, 1U, kue::EnemySpawnArea::Inside}}) ==
                   InvalidPayload,
               "SpawnEnemy", "a zero catalog generation must be rejected");
    run.expect(kue::validatePlayerActionRequest(
                   {kue::PlayerAction::SpawnEnemy,
                    kue::EnemySpawnPayload{{{7U}, 256U}, 1U, kue::EnemySpawnArea::Inside}}) ==
                   InvalidPayload,
               "SpawnEnemy", "an out-of-range catalog index must be rejected");
    run.expect(kue::validatePlayerActionRequest(
                   {kue::PlayerAction::SpawnEnemy,
                    kue::EnemySpawnPayload{kEnemyType, 0U, kue::EnemySpawnArea::Inside}}) ==
                   InvalidPayload,
               "SpawnEnemy", "zero count must be rejected");
    run.expect(kue::validatePlayerActionRequest(
                   {kue::PlayerAction::SpawnEnemy,
                    kue::EnemySpawnPayload{kEnemyType, 21U, kue::EnemySpawnArea::Inside}}) ==
                   InvalidPayload,
               "SpawnEnemy", "count above twenty must be rejected");
    run.expect(kue::validatePlayerActionRequest(
                   {kue::PlayerAction::SpawnEnemy,
                    kue::EnemySpawnPayload{kEnemyType, 1U, kue::EnemySpawnArea::Inside}}) == Valid,
               "SpawnEnemy", "count one must be accepted");
    run.expect(kue::validatePlayerActionRequest(
                   {kue::PlayerAction::SpawnEnemy,
                    kue::EnemySpawnPayload{kEnemyType, 20U, kue::EnemySpawnArea::Outside}}) ==
                   Valid,
               "SpawnEnemy", "count twenty must be accepted");
    run.expect(kue::validatePlayerActionRequest(
                   {kue::PlayerAction::SpawnEnemyAtPlayer,
                    kue::EnemyAtPlayerPayload{kEnemyType, 0U, kue::EnemySpawnArea::Inside, 17U}}) ==
                   InvalidPayload,
               "SpawnEnemyAtPlayer", "zero count must be rejected");
    run.expect(kue::validatePlayerActionRequest(
                   {kue::PlayerAction::SpawnEnemyAtPlayer,
                    kue::EnemyAtPlayerPayload{kEnemyType, 21U, kue::EnemySpawnArea::Outside,
                                              17U}}) == InvalidPayload,
               "SpawnEnemyAtPlayer", "count above twenty must be rejected");
    run.expect(kue::validatePlayerActionRequest(
                   {kue::PlayerAction::SpawnEnemyAtPlayer,
                    kue::EnemyAtPlayerPayload{kEnemyType, 20U, kue::EnemySpawnArea::Outside,
                                              17U}}) == Valid,
               "SpawnEnemyAtPlayer", "count twenty must be accepted");
    run.expect(kue::validatePlayerActionRequest({kue::PlayerAction::SpawnItemAtPlayer,
                                                 kue::ItemAtPlayerPayload{kItemType, 0U, 17U}}) ==
                   InvalidPayload,
               "SpawnItemAtPlayer", "zero count must be rejected");
    run.expect(kue::validatePlayerActionRequest({kue::PlayerAction::SpawnItemAtPlayer,
                                                 kue::ItemAtPlayerPayload{kItemType, 21U, 17U}}) ==
                   InvalidPayload,
               "SpawnItemAtPlayer", "count above twenty must be rejected");
    run.expect(kue::validatePlayerActionRequest({kue::PlayerAction::SpawnItemAtPlayer,
                                                 kue::ItemAtPlayerPayload{kItemType, 20U, 17U}}) ==
                   Valid,
               "SpawnItemAtPlayer", "count twenty must be accepted");
    run.expect(kue::validatePlayerActionRequest(
                   {kue::PlayerAction::TogglePjManSpam,
                    kue::PlushieIntervalPayload{std::chrono::milliseconds{-1}}}) == InvalidPayload,
               "TogglePjManSpam", "negative interval must be rejected");
    run.expect(kue::validatePlayerActionRequest(
                   {kue::PlayerAction::TogglePjManSpam,
                    kue::PlushieIntervalPayload{std::chrono::milliseconds{1001}}}) ==
                   InvalidPayload,
               "TogglePjManSpam", "interval above one second must be rejected");
    run.expect(kue::validatePlayerActionRequest(
                   {kue::PlayerAction::TogglePjManSpam,
                    kue::PlushieIntervalPayload{std::chrono::milliseconds{0}}}) == Valid,
               "TogglePjManSpam", "zero interval must be accepted");
    run.expect(kue::validatePlayerActionRequest(
                   {kue::PlayerAction::TogglePjManSpam,
                    kue::PlushieIntervalPayload{std::chrono::milliseconds{1000}}}) == Valid,
               "TogglePjManSpam", "one-second interval must be accepted");
    run.expect(kue::validatePlayerActionRequest(
                   {kue::PlayerAction::Kill,
                    kue::PlayerTargetPayload{std::numeric_limits<std::uint64_t>::max()}}) == Valid,
               "Kill", "the exact network client ID width must be preserved");
}

static_assert(!std::is_constructible_v<kue::PlayerActionPayload, int>);
static_assert(!std::is_constructible_v<kue::PlayerActionRequest, kue::PlayerAction, int>);
static_assert(std::is_same_v<decltype(kue::EnemySpawnPayload::enemyType), kue::EnemyTypeId>);
static_assert(std::is_same_v<decltype(kue::EnemyAtPlayerPayload::enemyType), kue::EnemyTypeId>);
static_assert(std::is_same_v<decltype(kue::ItemAtPlayerPayload::itemType), kue::ItemTypeId>);
static_assert(std::variant_size_v<kue::PlayerActionPayload> == kPayloadKinds.size());
static_assert(static_cast<std::size_t>(kue::PlayerAction::MaxInsanity) == kActionContracts.size());

}

int main() {
    TestRun run;
    testActionPayloadContracts(run);
    testPayloadBoundaries(run);
    return run.result();
}
