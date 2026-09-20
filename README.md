# KUE-LETHAL

Kue Lethal is an internal Lethal Company menu for x86-64 Linux and Windows. On Linux it runs inside the Proton/Wine game process; on Windows it runs inside the native game process. Either way it installs a managed Unity HUD that renders the ImGui menu and ESP in the game frame. FYI: This was only made in mind for me

## Features

| Area | Features |
|---|---|
| Self | Infinite stamina, no weight, battery, expanded inventory, fly (with flapping arms), third person, heal, and kill |
| ESP | Players, items, enemies, entrances, fire exits, and ship silhouettes and labels |
| Players | Teleport, kill, heal, insanity, one-shot lure, and persistent targeting |
| Enemies | Runtime catalog spawning, targeted spawning, teleporting, stunning, and killing |
| Items | Runtime catalog spawning, teleporting to a selected player, depositing all ship scrap on the company desk, and adding terminal credits (host) |
| Trolls | Ship, factory, terminal, mine, turret, bridge, vehicle, shotgun, and map actions |

ESP settings include silhouettes, labels, scrap values, distances, tracer lines, maximum distance, scrap-tier colors, and category colors. The runtime catalogs enemies and items from the installed game assets rather than a fixed historical list.

## Requirements

The build host is Linux x86-64 for both targets.

- GCC or Clang with C++20 support
- CMake 3.24 or newer
- pkg-config, Capstone 5, Wine, and Wine Mono
- Lethal Company installed through Steam with its managed assemblies available
- gdb only for attaching to an already-running game on Linux
- For the Windows target: the MinGW-w64 x86-64 GCC toolchain (`x86_64-w64-mingw32-g++`); Wine
  also runs the Windows test executables

The build locates the game under common native and Flatpak Steam paths. Set
`KUE_GAME_MANAGED_DIR` to the game's `Lethal Company_Data/Managed` directory when it is elsewhere. Set `KUE_CSC` to Wine Mono's `csc.exe` if compiler discovery is ambiguous. `KUE_STEAM_COMMON` can
override the Steam library root.

## Building

```bash
scripts/build.sh
```

The command produces:

- `build/kuelethal.so`
- `build/managed/KueInternalHud.dll`

The Windows target is cross-compiled from the same tree:

```bash
TARGET=windows scripts/build.sh
```

It produces:

- `build-windows/kuelethal.dll`
- `build-windows/kue-inject.exe`
- `build-windows/managed/KueInternalHud.dll`

Copy the `build-windows` directory and the `config` directory next to each other onto the
Windows machine, keeping `kuelethal.dll`, `kue-inject.exe`, and `managed/KueInternalHud.dll`
together. Capstone 5.0.9 is fetched and built from source for the Windows target; set
`KUE_CAPSTONE_SOURCE_DIR` together with `KUE_FETCH_DEPS=OFF` for an offline build.

The default build fetches Dear ImGui and nlohmann/json at immutable commits. For an offline build,
provide exact nlohmann/json 3.11.3 and ImGui 1.91.9b source trees:

```bash
cmake -S . -B build \
  -DKUE_FETCH_DEPS=OFF \
  -DKUE_JSON_SOURCE_DIR=/path/to/json-3.11.3 \
  -DKUE_IMGUI_SOURCE_DIR=/path/to/imgui-1.91.9b \
  -DKUE_GAME_MANAGED_DIR=/path/to/Lethal\ Company_Data/Managed
cmake --build build --parallel
```

Both build modes compile project and bundled dependency sources with warnings treated as errors. To build only the dependency-independent native guards without Capstone, ImGui, game assemblies, or the production module:

```bash
cmake -S . -B build-tests -DKUE_BUILD_MODULE=OFF
cmake --build build-tests --parallel
ctest --test-dir build-tests --output-on-failure
```

## Loading at process start

The current module has an explicit `kue_start` entry and performs no work merely from being mapped.
Plain `LD_PRELOAD` and `scripts/run.sh` therefore do not start the HUD. The native startup-profiler
launcher is still under development; use running-game injection for the current intermediate build.
`scripts/run.sh` currently validates and exports module, configuration, and log paths only.

By default, scripts write logs to
`${XDG_STATE_HOME:-$HOME/.local/state}/kuelethal/kuelethal.log`. Set `KUE_LOG` to override it and
`KUE_CONSOLE=1` to mirror runtime logs to the process console.

## Loading into a running game

Launch Lethal Company first, then run:

```bash
scripts/inject.sh
```

On Windows, launch Lethal Company and run `kue-inject.exe` from the `build-windows` directory (a
double-click works). It finds the `Lethal Company.exe` process, refuses to load a second copy,
loads `kuelethal.dll`, hands it the configuration and log paths through the exported
`kue_start_remote` entry, and confirms the build identity and HUD delivery through the log exactly
like the Linux injector. `KUE_MODULE`, `KUE_CONFIG`, `KUE_LOG`, and `GAME_PATTERN` (an exact
executable name on Windows) override the defaults; the default log is
`%LOCALAPPDATA%\kuelethal\kuelethal.log` and the default configuration is
`..\config\kuelethal.json` relative to the injector. A failed start restores the target's
environment and unloads the module.

`GAME_PATTERN` selects a different process pattern. `KUE_MODULE`, `KUE_CONFIG`, and `KUE_LOG`
override the artifact and target-process settings. The injector writes `KUE_CONFIG` and `KUE_LOG`
into the attached process before calling `dlopen`, immediately detaches, and verifies the exact build
identity through the configured log. It refuses to load a second copy into the same process.

Attaching requires ptrace permission. If attachment is prohibited, no current loader starts the HUD;
the native startup-profiler launcher must be completed instead of weakening the host's ptrace policy.

## Menu and configuration

Press Insert to open or close the menu. Escape also closes it and restores the captured game input
state.

- `Self`: local movement, inventory, health, and death controls
- `Visuals`: ESP categories, silhouettes, labels, lines, distance, and colors
- `Players`: per-player actions and enemy targeting
- `Enemies`: spawning and global enemy actions
- `Items`: spawning and teleporting items
- `Trolls`: host and client-side world actions
- `Settings`: menu key, update rates, and configuration saving

The bundled configuration is `config/kuelethal.json`. `KUE_CONFIG` selects another file. Without an explicit path, the runtime also checks `$HOME/.config/kuelethal/config.json` on Linux,
`%APPDATA%\kuelethal\config.json` on Windows, and the process working directory.

## Project layout

```text
config/       default runtime configuration
cmake/        pinned dependency declarations
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
