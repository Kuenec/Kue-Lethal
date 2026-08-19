#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
output="${1:-$project_root/build/managed/KueInternalHud.dll}"

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        printf 'error: required command is unavailable: %s\n' "$1" >&2
        exit 1
    fi
}

required_commands=(
    cat dirname find mkdir mktemp mv rm rmdir sort
    wine winepath
)
for command_name in "${required_commands[@]}"; do
    require_command "$command_name"
done

managed="${KUE_GAME_MANAGED_DIR:-}"
if [[ -z "$managed" ]]; then
    if [[ -z "${HOME:-}" ]]; then
        printf '%s%s\n' \
            'error: HOME is unset; set KUE_GAME_MANAGED_DIR ' \
            'explicitly' >&2
        exit 1
    fi
    data_home="${XDG_DATA_HOME:-$HOME/.local/share}"
    steam_common="${KUE_STEAM_COMMON:-$data_home/Steam/steamapps/common}"
    steam_game="$HOME/.steam/steam/steamapps/common/Lethal Company"
    flatpak_steam="$HOME/.var/app/com.valvesoftware.Steam/data/Steam"
    flatpak_game="$flatpak_steam/steamapps/common/Lethal Company"
    candidates=(
        "$steam_common/Lethal Company/Lethal Company_Data/Managed"
        "$steam_game/Lethal Company_Data/Managed"
        "$flatpak_game/Lethal Company_Data/Managed"
    )
    for candidate in "${candidates[@]}"; do
        if [[ -f "$candidate/Assembly-CSharp.dll" ]]; then
            managed="$candidate"
            break
        fi
    done
fi
if [[ -z "$managed" || ! -f "$managed/Assembly-CSharp.dll" ]]; then
    printf '%s%s\n' \
        'error: Lethal Company managed assemblies were not found; set ' \
        'KUE_GAME_MANAGED_DIR to Lethal Company_Data/Managed' >&2
    exit 1
fi

compiler="${KUE_CSC:-}"
if [[ -z "$compiler" ]]; then
    mapfile -t compilers < <(
        find /usr/share/wine/mono -type f \
            -path '*/lib/mono/4.5/csc.exe' -print 2>/dev/null |
            sort
    )
    if ((${#compilers[@]} == 1)); then
        compiler="${compilers[0]}"
    elif ((${#compilers[@]} > 1)); then
        printf '%s%s\n' \
            'error: multiple Wine Mono C# compilers found; ' \
            'set KUE_CSC to the intended csc.exe' >&2
        printf '  %s\n' "${compilers[@]}" >&2
        exit 1
    fi
fi
if [[ -z "$compiler" || ! -f "$compiler" ]]; then
    printf '%s%s\n' \
        'error: Wine Mono C# compiler was not found; install Wine Mono or ' \
        'set KUE_CSC to its csc.exe' >&2
    exit 1
fi

references=(
    mscorlib.dll
    netstandard.dll
    System.dll
    Assembly-CSharp.dll
    Unity.Netcode.Runtime.dll
    UnityEngine.dll
    UnityEngine.CoreModule.dll
    UnityEngine.IMGUIModule.dll
    UnityEngine.AIModule.dll
    UnityEngine.InputLegacyModule.dll
    UnityEngine.PhysicsModule.dll
    UnityEngine.TextRenderingModule.dll
    Unity.InputSystem.dll
    UnityEngine.UI.dll
)
for reference in "${references[@]}"; do
    if [[ ! -f "$managed/$reference" ]]; then
        printf 'error: required managed assembly is missing: %s\n' \
            "$managed/$reference" >&2
        exit 1
    fi
done

source_file="$project_root/src/managed/KueInternalHud.cs"
if [[ ! -f "$source_file" ]]; then
    printf 'error: managed source is missing: %s\n' "$source_file" >&2
    exit 1
fi

output_directory="$(dirname "$output")"
mkdir -p "$output_directory"
temporary_directory="$(mktemp -d "$output_directory/.kue-managed.XXXXXX")"
compiler_log="$temporary_directory/compiler.log"
temporary_output="$temporary_directory/KueInternalHud.dll"
cleanup() {
    local operation_status=$?
    local cleanup_status=0
    if ! rm -f "$compiler_log" "$temporary_output"; then
        printf >&2 \
            'error: cannot remove managed-build temporary files from: %s\n' \
            "$temporary_directory"
        cleanup_status=1
    fi
    if [[ -d "$temporary_directory" ]] && ! rmdir "$temporary_directory"; then
        printf >&2 \
            'error: cannot remove managed-build temporary directory: %s\n' \
            "$temporary_directory"
        cleanup_status=1
    fi
    if ((operation_status != 0)); then
        return "$operation_status"
    fi
    return "$cleanup_status"
}
trap cleanup EXIT

source_win="$(
    DISPLAY= WAYLAND_DISPLAY= LIBGL_ALWAYS_SOFTWARE=true \
        winepath -w "$source_file"
)"
output_win="$(
    DISPLAY= WAYLAND_DISPLAY= LIBGL_ALWAYS_SOFTWARE=true \
        winepath -w "$temporary_output"
)"
managed_win="$(
    DISPLAY= WAYLAND_DISPLAY= LIBGL_ALWAYS_SOFTWARE=true \
        winepath -w "$managed"
)"

arguments=()
for reference in "${references[@]}"; do
    arguments+=("/r:$managed_win\\$reference")
done

if ! DISPLAY= WAYLAND_DISPLAY= LIBGL_ALWAYS_SOFTWARE=true wine "$compiler" \
    /nologo \
    /target:library \
    /optimize+ \
    /debug- \
    /deterministic+ \
    /warn:4 \
    /warnaserror+ \
    /langversion:9.0 \
    /nostdlib+ \
    "/out:$output_win" \
    "${arguments[@]}" \
    "$source_win" >"$compiler_log" 2>&1; then
    cat "$compiler_log" >&2
    exit 1
fi
if [[ -s "$compiler_log" ]]; then
    cat "$compiler_log" >&2
fi
if [[ ! -s "$temporary_output" ]]; then
    printf 'error: C# compiler did not produce a nonempty assembly: %s\n' \
        "$temporary_output" >&2
    exit 1
fi
mv -f "$temporary_output" "$output"
