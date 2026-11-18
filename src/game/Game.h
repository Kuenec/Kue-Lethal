#ifndef KUE_GAME_GAME_H
#define KUE_GAME_GAME_H

#include "game/PlayerSnapshot.h"

#include <cstdint>
#include <optional>

namespace kue {

struct PlayerFrameSnapshot {
    float sprintMeter = 0.f;
    float carryWeight = 0.f;
    PlayerSnapshot players;
};

enum class SessionSwitch : std::uint8_t {
    Fly,
    ThirdPerson,
    EmoteWhileMoving,
    HearEveryone,
    ShipHorn,
    ShipLights,
    TerminalSound,
    ShipDoorButtons,
    CruiserMagnet,
    PlushieSpam,
    CruiserHorn,
    DeskDoorSound,
    LandminesArmed,
    TurretsActive,
    FacilityPower,
    ShotgunSpam,
    JetpackTrap,
    Count,
};

enum class WorldFeature : std::uint8_t {
    Host,
    ShipInOrbit,
    MoonLoaded,
    PlayersCanDie,
    EnemiesAlive,
    ShipHorn,
    ShipLights,
    Terminal,
    Plushie,
    Cruiser,
    CompanyDesk,
    Landmines,
    Turrets,
    FacilityPower,
    MainBridge,
    SmallBridge,
    MineshaftElevator,
    Shotguns,
    Jetpacks,
    HauntedMasks,
    HoardingBugNest,
    ShipDoor,
    ModelPreviews,
    Count,
};

template <typename Flag> class FlagSet final {
  public:
    constexpr FlagSet() noexcept = default;

    [[nodiscard]] static constexpr FlagSet fromBits(std::uint32_t bits) noexcept {
        constexpr std::uint32_t valid =
            (std::uint32_t{1} << static_cast<unsigned>(Flag::Count)) - 1U;
        FlagSet set;
        set.mBits = bits & valid;
        return set;
    }

    [[nodiscard]] constexpr bool has(Flag flag) const noexcept {
        return (mBits & (std::uint32_t{1} << static_cast<unsigned>(flag))) != 0;
    }

    constexpr void set(Flag flag) noexcept {
        mBits |= std::uint32_t{1} << static_cast<unsigned>(flag);
    }

    friend constexpr bool operator==(FlagSet, FlagSet) = default;

  private:
    std::uint32_t mBits = 0;
};

static_assert(static_cast<unsigned>(SessionSwitch::Count) < 32U);
static_assert(static_cast<unsigned>(WorldFeature::Count) < 32U);

enum class PlayerLocation : std::uint8_t { Outside, Facility };

struct EnemyLure {
    std::uint64_t clientId = 0;
    std::optional<int> secondsLeft;
};

struct SessionState {
    FlagSet<SessionSwitch> switches;
    FlagSet<WorldFeature> world;
    std::uint64_t playersInFacility = 0;
    std::optional<EnemyLure> lure;

    [[nodiscard]] constexpr PlayerLocation location(std::uint64_t clientId) const noexcept {
        if (clientId < 64U && (playersInFacility & (std::uint64_t{1} << clientId)) != 0) {
            return PlayerLocation::Facility;
        }
        return PlayerLocation::Outside;
    }
};

struct MenuSnapshot {
    PlayerFrameSnapshot playerFrame;
    SessionState session;
};

}

#endif
