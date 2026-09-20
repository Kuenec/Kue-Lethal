#include "main.h"

#include "platform/Environment.h"

namespace kue {

BootResult boot() {
    platform::EnvironmentStorage storage;
    const platform::EnvironmentValue requested =
        platform::readEnvironment("KUE_FIXTURE_BOOT", storage);
    if (requested.status == platform::EnvironmentStatus::Valid &&
        requested.text == "state-start-failure") {
        return BootResult::StateStartFailed;
    }
    return BootResult::Running;
}

}
