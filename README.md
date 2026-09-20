# Kue Lethal

An in-process menu for Lethal Company on Linux (Proton) and Windows. It loads into the running game, installs a small managed HUD inside Unity, and draws its ImGui menu and ESP directly in the game frame. There is no external overlay window and nothing to alt-tab to.

This is a personal project. It works well for me; it may or may not work for you.

## What it does

| Area | Features |
|---|---|
| Self | Infinite stamina, no weight, infinite battery, expanded inventory, fly (with flapping arms), third person, heal, kill |
| Visuals | ESP for players, items, enemies, entrances, fire exits, and the ship, with model outlines, names, scrap values, distances, and tracer lines |
| Players | Teleport, kill, heal, insanity control, one-shot enemy lure, and persistent targeting |
| Enemies | Spawn any installed enemy type, spawn or teleport enemies onto a player, stun or kill everything |
| Items | Spawn any installed item, teleport loose items to a player, deposit all ship scrap on the company desk, add terminal credits (host) |
| Trolls | Ship, factory, terminal, landmine, turret, bridge, vehicle, shotgun, and company-desk actions |

Enemy and item lists are read from the installed game assets at runtime, so new content shows up without an update here. ESP outlines are rendered by the game's own pipeline, which means they follow the model exactly and stay visible through walls.

## Requirements

Builds happen on Linux x86-64 for both targets.

- GCC or Clang with C++20 support, CMake 3.24 or newer, pkg-config
- Capstone 5, Wine, and Wine Mono (the managed HUD is compiled with Wine Mono's C# compiler)
- Lethal Company installed through Steam; the build reads its managed assemblies
- For the Windows target: the MinGW-w64 x86-64 toolchain (`x86_64-w64-mingw32-g++`)
- For attaching on Linux: gdb

The build finds the game under the usual native and Flatpak Steam paths and in any library listed in `libraryfolders.vdf`. If it can't, set `KUE_GAME_MANAGED_DIR` to the game's `Lethal Company_Data/Managed` directory. `KUE_CSC` pins a specific Wine Mono `csc.exe`, and `KUE_STEAM_COMMON` overrides the Steam library root.

## Building

Linux:

```bash
scripts/build.sh
```

produces `build/kuelethal.so` and `build/managed/KueInternalHud.dll`.

Windows (cross-compiled from the same tree):

```bash
TARGET=windows scripts/build.sh
```

produces `build-windows/kuelethal.dll`, `build-windows/kue-inject.exe`, and `build-windows/managed/KueInternalHud.dll`. Copy the `build-windows` and `config` directories side by side onto the Windows machine; the DLL, the injector, and `managed/KueInternalHud.dll` need to stay together.

Dependencies (Dear ImGui, nlohmann/json, and Capstone for the Windows target) are fetched at pinned versions. For an offline build, point the build at exact source trees:

```bash
cmake -S . -B build \
  -DKUE_FETCH_DEPS=OFF \
  -DKUE_JSON_SOURCE_DIR=/path/to/json-3.11.3 \
  -DKUE_IMGUI_SOURCE_DIR=/path/to/imgui-1.91.9b \
  -DKUE_GAME_MANAGED_DIR=/path/to/Lethal\ Company_Data/Managed
cmake --build build --parallel
```

Add `-DKUE_CAPSTONE_SOURCE_DIR=/path/to/capstone-5.0.9` for an offline Windows build.

Project sources compile with warnings treated as errors. To build and run only the dependency-free native tests:

```bash
cmake -S . -B build-tests -DKUE_BUILD_MODULE=OFF
cmake --build build-tests --parallel
ctest --test-dir build-tests --output-on-failure
```

The Windows test suite runs under Wine: `cmake --build build-windows --parallel && ctest --test-dir build-windows --output-on-failure`.

## Loading into the game

Start Lethal Company first, then load the module.

**Linux**

```bash
scripts/inject.sh
```

The script attaches with gdb, writes `KUE_CONFIG` and `KUE_LOG` into the game process, loads the module, calls its start entry, detaches, and then confirms through the log that the exact build it loaded came up. It refuses to load a second copy into the same process. Attaching needs ptrace permission; if that is prohibited on your system there is currently no other loader.

**Windows**

Run `kue-inject.exe` from the `build-windows` directory (double-clicking it is fine). It finds the `Lethal Company.exe` process, loads `kuelethal.dll`, hands over the configuration and log paths, and confirms the build identity and HUD delivery through the log, the same way the Linux script does. A failed start restores the game's environment and unloads the module.

Both loaders honor the same overrides: `KUE_MODULE`, `KUE_CONFIG`, `KUE_LOG`, and `GAME_PATTERN` (a process pattern on Linux, an exact executable name on Windows). Default log locations are `${XDG_STATE_HOME:-$HOME/.local/state}/kuelethal/kuelethal.log` on Linux and `%LOCALAPPDATA%\kuelethal\kuelethal.log` on Windows. Set `KUE_CONSOLE=1` to mirror runtime logs to the process console.

`scripts/run.sh` only validates and exports the module, configuration, and log paths; the module never starts from merely being mapped, so a plain `LD_PRELOAD` does not start the HUD.

## Using the menu

Press Insert to open or close the menu. Escape also closes it and restores the game's input state.

- **Self**: movement, inventory, health, fly, third person, and death controls
- **Visuals**: ESP categories, outlines, labels, lines, distance, and colors
- **Players**: per-player actions and enemy targeting
- **Enemies**: spawning and global enemy actions
- **Items**: spawning, teleporting, depositing scrap, and terminal credits
- **Trolls**: host and client-side world actions
- **Settings**: menu key, update rates, and configuration saving

Some actions are host only because the game only lets the host perform them; the menu greys those out when you join as a client.

## Configuration

The bundled configuration is `config/kuelethal.json`. `KUE_CONFIG` selects another file. Without an explicit path, the runtime also checks `$HOME/.config/kuelethal/config.json` on Linux, `%APPDATA%\kuelethal\config.json` on Windows, and the process working directory. Changes made in the menu are saved back automatically.

## Project layout

```text
config/       default runtime configuration
cmake/        pinned dependency declarations and the MinGW toolchain
scripts/      native, managed, launch, and live-attach commands
src/core/     configuration and logging
src/entry/    injectable module entry points
src/game/     game snapshots, actions, and runtime state
src/inject/   native Windows injector
src/mono/     Mono runtime access, Unity metadata, and PE export parsing
src/managed/  Unity HUD, input, actions, and ESP
src/overlay/  in-frame ImGui menu, theme, and rasterization
src/platform/ operating-system services with POSIX and Windows implementations
tests/        subject-specific native regression tests
```

Third-party attribution is recorded in `NOTICE`.
