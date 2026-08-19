#ifndef KUE_OVERLAY_INTERNAL_HUD_H
#define KUE_OVERLAY_INTERNAL_HUD_H

namespace kue {
struct Config;
class LethalState;
}

namespace kue::internalhud {

void initialize(LethalState& source, Config& config);
bool registerManagedBridge();

}

#endif
