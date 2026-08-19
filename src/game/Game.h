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

struct MenuSnapshot {
    PlayerFrameSnapshot playerFrame;
    std::optional<std::uint64_t> persistentLureClientId;
    bool flyEnabled = false;
    bool localIsHost = false;
};

}

#endif
