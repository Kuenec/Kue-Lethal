#!/usr/bin/env bash
set -euo pipefail

script_path="${BASH_SOURCE[0]}"
if [[ "$script_path" == */* ]]; then
    script_directory="${script_path%/*}"
else
    script_directory=.
fi
project_root="$(
    cd -- "$script_directory/.." || exit 1
    printf '%s\034' "$PWD"
)"
project_root="${project_root%$'\034'}"
entry_error_prefix='[kue] '
entry_validation="$project_root/scripts/entry-validation.sh"
if [[ ! -f "$entry_validation" || ! -r "$entry_validation" ]]; then
    printf '[kue] error: entry validation is unavailable: %s\n' \
        "$entry_validation" >&2
    exit 1
fi
source "$entry_validation"

windows_artifact() {
    local destination="$1"
    local variable="$2"
    local default_path="$3"
    if [[ -v "$variable" && -z "${!variable}" ]]; then
        printf '[kue] error: %s is set but empty\n' "$variable" >&2
        exit 1
    fi
    printf -v "$destination" '%s' "${!variable-$default_path}"
}

windows_artifact injector KUE_INJECTOR "$project_root/build-windows/kue-inject.exe"
windows_artifact module KUE_MODULE "$project_root/build-windows/kuelethal.dll"
if [[ -v KUE_CONFIG && -z "$KUE_CONFIG" ]]; then
    printf '%s\n' '[kue] error: KUE_CONFIG is set but empty' >&2
    exit 1
fi
if [[ -v KUE_LOG && -z "$KUE_LOG" ]]; then
    printf '%s\n' '[kue] error: KUE_LOG is set but empty' >&2
    exit 1
fi
if [[ ! -v KUE_LOG && -v XDG_STATE_HOME && -z "$XDG_STATE_HOME" ]]; then
    printf '%s\n' \
        '[kue] error: XDG_STATE_HOME is empty; set KUE_LOG explicitly' \
        >&2
    exit 1
fi
if [[ ! -v KUE_LOG && ! -v XDG_STATE_HOME && -z "${HOME:-}" ]]; then
    printf '%s\n' \
        '[kue] error: HOME/XDG_STATE_HOME unset; set KUE_LOG explicitly' \
        >&2
    exit 1
fi
if [[ -v GAME_PATTERN && -z "$GAME_PATTERN" ]]; then
    printf '%s\n' '[kue] error: GAME_PATTERN is set but empty' >&2
    exit 1
fi
state_home="${XDG_STATE_HOME-${HOME:-}/.local/state}"
KUE_CONFIG="${KUE_CONFIG-$project_root/config/kuelethal.json}"
KUE_LOG="${KUE_LOG-$state_home/kuelethal/kuelethal.log}"
GAME_PATTERN="${GAME_PATTERN-Lethal Company.exe}"

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        printf '[kue] error: required command is unavailable: %s\n' "$1" >&2
        exit 1
    fi
}

required_commands=(dirname file grep iconv mkdir pgrep readlink realpath tr)
for command_name in "${required_commands[@]}"; do
    require_command "$command_name"
done

validate_text "$project_root" 4096 "project root"
validate_text "$injector" 4096 "injector path"
validate_text "$module" 4096 "module path"
validate_text "$KUE_CONFIG" 4096 "configuration path"
validate_text "$KUE_LOG" 4096 "log path"
validate_text "$GAME_PATTERN" 256 "GAME_PATTERN"
if [[ -v KUE_INJECTOR && ! -f "$injector" ]]; then
    printf '[kue] error: configured injector not found: %s\n' "$injector" >&2
    exit 1
fi
if [[ -v KUE_MODULE && ! -f "$module" ]]; then
    printf '[kue] error: configured module not found: %s\n' "$module" >&2
    exit 1
fi
if [[ ! -f "$KUE_CONFIG" || ! -r "$KUE_CONFIG" ]]; then
    printf '[kue] error: configuration file not found: %s\n' "$KUE_CONFIG" >&2
    exit 1
fi
if ! canonicalize_existing "$KUE_CONFIG" KUE_CONFIG; then
    printf '[kue] error: cannot resolve configuration file: %s\n' \
        "$KUE_CONFIG" >&2
    exit 1
fi
validate_text "$KUE_CONFIG" 4096 "resolved configuration path"
log_name="${KUE_LOG##*/}"
if [[ -z "$log_name" || "$log_name" == "." || "$log_name" == ".." ]]; then
    printf '[kue] error: log path does not name a file: %s\n' "$KUE_LOG" >&2
    exit 1
fi
log_directory="$(dirname -- "$KUE_LOG")"
if ! mkdir -p -- "$log_directory"; then
    printf '[kue] error: cannot create log directory: %s\n' \
        "$log_directory" >&2
    exit 1
fi
if ! canonicalize_existing "$log_directory" log_directory; then
    printf '[kue] error: cannot resolve log directory: %s\n' \
        "$log_directory" >&2
    exit 1
fi
if [[ "$log_directory" == "/" ]]; then
    KUE_LOG="/$log_name"
else
    KUE_LOG="$log_directory/$log_name"
fi
validate_text "$KUE_LOG" 4096 "resolved log path"
if [[ (-e "$KUE_LOG" || -L "$KUE_LOG") && ! -f "$KUE_LOG" ]]; then
    printf '[kue] error: log target is not a regular file: %s\n' "$KUE_LOG" >&2
    exit 1
fi
if [[ -e "$KUE_LOG" ]]; then
    if [[ ! -w "$KUE_LOG" ]]; then
        printf '[kue] error: log target is not writable: %s\n' "$KUE_LOG" >&2
        exit 1
    fi
    if ! canonicalize_existing "$KUE_LOG" KUE_LOG; then
        printf '[kue] error: cannot resolve log target: %s\n' "$KUE_LOG" >&2
        exit 1
    fi
    validate_text "$KUE_LOG" 4096 "resolved log path"
elif [[ ! -w "$log_directory" ]]; then
    printf '[kue] error: log directory is not writable: %s\n' \
        "$log_directory" >&2
    exit 1
fi

if [[ ! -f "$injector" || ! -f "$module" ]]; then
    printf '%s\n' '[kue] Windows build not found - building first'
    TARGET=windows "$project_root/scripts/build.sh"
fi

require_portable_executable() {
    local path="$1"
    local expected="$2"
    local description
    if [[ ! -f "$path" || ! -r "$path" ]]; then
        printf '[kue] error: not a readable regular file: %s\n' "$path" >&2
        exit 1
    fi
    if ! description="$(LC_ALL=C file -Lb -- "$path")"; then
        printf '[kue] error: cannot inspect %s\n' "$path" >&2
        exit 1
    fi
    if [[ "$description" != "PE32+ executable"*"$expected"*"x86-64"* ]]; then
        printf '[kue] error: %s is not an x86-64 Windows %s: %s\n' \
            "$path" "$expected" "$description" >&2
        exit 1
    fi
}

require_portable_executable "$injector" "console"
require_portable_executable "$module" "DLL"
if ! canonicalize_existing "$injector" injector ||
    ! canonicalize_existing "$module" module; then
    printf '%s\n' '[kue] error: cannot resolve the Windows build artifacts' >&2
    exit 1
fi

required_architecture=64
candidate_pids=()

collect_candidate_pids() {
    local -a process_search_records=()
    local record search_status_text=""
    local search_status=0
    candidate_pids=()
    if ! mapfile -t -n 258 process_search_records < <(
        local command_status=0
        pgrep -f -- "$GAME_PATTERN" || command_status=$?
        printf 'KUE_PROCESS_SEARCH_STATUS=%d\n' "$command_status"
    ); then
        printf '%s%s\n' \
            '[kue] error: cannot collect process candidates for pattern: ' \
            "$GAME_PATTERN" >&2
        return 2
    fi
    for record in "${process_search_records[@]}"; do
        if [[ "$record" =~ ^KUE_PROCESS_SEARCH_STATUS=([0-9]{1,3})$ ]]; then
            if [[ -n "$search_status_text" ]]; then
                printf '%s\n' \
                    '[kue] error: process search returned duplicate status' >&2
                return 2
            fi
            search_status_text="${BASH_REMATCH[1]}"
            continue
        fi
        if [[ ! "$record" =~ ^[1-9][0-9]*$ ]]; then
            printf '[kue] error: process search returned invalid pid: %s\n' \
                "$record" >&2
            return 2
        fi
        candidate_pids+=("$record")
        if ((${#candidate_pids[@]} > 256)); then
            printf '%s\n' \
                '[kue] error: process search exceeded 256 candidates' >&2
            return 2
        fi
    done
    if [[ -z "$search_status_text" ]]; then
        printf '%s\n' \
            '[kue] error: process search returned no completion status' >&2
        return 2
    fi
    search_status=$((10#$search_status_text))
    if ((search_status == 0)); then
        if ((${#candidate_pids[@]} == 0)); then
            printf '%s\n' \
                '[kue] error: process search succeeded without a candidate' >&2
            return 2
        fi
        return 0
    fi
    if ((search_status == 1)) && ((${#candidate_pids[@]} == 0)); then
        return 1
    fi
    printf '[kue] error: process search failed for pattern: %s (status %d)\n' \
        "$GAME_PATTERN" "$search_status" >&2
    return 2
}

if collect_candidate_pids; then
    :
else
    process_search_status=$?
    if ((process_search_status != 1)); then
        exit 1
    fi
    echo "[kue] error: no running game matched '$GAME_PATTERN'"
    echo '[kue] launch Lethal Company first; process-start is not implemented'
    exit 1
fi

pick_target() {
    local p exe arch cmd
    local -a mapped_matches=()
    local -a command_matches=()
    for p in "${candidate_pids[@]}"; do
        cmd="$(tr '\0' ' ' <"/proc/$p/cmdline" 2>/dev/null || true)"
        case "$cmd" in
            *bash* | *pgrep* | *inject.sh*) continue ;;
        esac
        exe="$(readlink "/proc/$p/exe" 2>/dev/null || true)"
        if [[ -z "$exe" ]]; then
            if command -v sudo >/dev/null 2>&1; then
                if sudo -n true 2>/dev/null; then
                    exe="$(
                        sudo -n readlink "/proc/$p/exe" 2>/dev/null || true
                    )"
                fi
            fi
        fi
        [[ -z "$exe" ]] && continue
        arch="$(LC_ALL=C file -b "$exe" 2>/dev/null || true)"
        case "$arch" in
            *"ELF ${required_architecture}-bit"*) ;;
            *) continue ;;
        esac
        if grep -q 'UnityPlayer' "/proc/$p/maps" 2>/dev/null; then
            mapped_matches+=("$p")
            continue
        fi
        case "$cmd" in
            *"/Lethal Company/Lethal Company.exe"* | \
                *"\\Lethal Company\\Lethal Company.exe"*)
                command_matches+=("$p")
                ;;
        esac
    done
    local -a matches=()
    if ((${#mapped_matches[@]} != 0)); then
        matches=("${mapped_matches[@]}")
    else
        matches=("${command_matches[@]}")
    fi
    if ((${#matches[@]} == 1)); then
        echo "${matches[0]}"
        return 0
    fi
    if ((${#matches[@]} > 1)); then
        printf '[kue] error: multiple matching game processes found: %s\n' \
            "${matches[*]}" >&2
        return 2
    fi
    return 1
}

if target="$(pick_target)"; then
    :
else
    target_status=$?
    if ((target_status == 2)); then
        exit 1
    fi
    target=""
fi

if [[ -z "$target" ]]; then
    printf '%s%s\n' \
        "[kue] error: no 64-bit Unity/game process matching '$GAME_PATTERN' " \
        "found (candidates: ${candidate_pids[*]})"
    echo "[kue] hint: match it yourself with: GAME_PATTERN='<name>' $0"
    exit 1
fi

if [[ ! /proc/$target/root -ef / ]]; then
    printf '%s%s\n' '[kue] error: the game runs in a container; ' \
        'launch it without the Steam Linux Runtime container' >&2
    exit 1
fi

game_environment=()
wine_prefix=""
while IFS= read -r -d '' entry; do
    case "$entry" in
        WINEPREFIX=*)
            wine_prefix="${entry#WINEPREFIX=}"
            game_environment+=("$entry")
            ;;
        WINE*=* | LD_LIBRARY_PATH=* | PATH=*) game_environment+=("$entry") ;;
    esac
done <"/proc/$target/environ"
if [[ -z "$wine_prefix" ]]; then
    printf '[kue] error: pid %s has no WINEPREFIX; is it running under Proton?\n' \
        "$target" >&2
    exit 1
fi
if [[ ! "$wine_prefix/dosdevices/z:" -ef / ]]; then
    printf '[kue] error: drive Z: of %s does not map to /\n' "$wine_prefix" >&2
    exit 1
fi

wine_directory="$(dirname -- "$(readlink "/proc/$target/exe")")"
wine=""
for wine_name in wine wine64; do
    if [[ -x "$wine_directory/$wine_name" ]]; then
        wine="$wine_directory/$wine_name"
        break
    fi
done
if [[ -z "$wine" ]]; then
    printf '[kue] error: no Wine loader next to the game process in %s\n' \
        "$wine_directory" >&2
    exit 1
fi

windows_path() {
    printf 'Z:%s' "${1//\//\\}"
}

passthrough_environment=()
for variable in HOME USER LANG DISPLAY WAYLAND_DISPLAY XDG_RUNTIME_DIR; do
    if [[ -v "$variable" ]]; then
        passthrough_environment+=("$variable=${!variable}")
    fi
done

printf '[kue] loading %s into pid %s through %s\n' "$module" "$target" "$wine"
exec env -i "${passthrough_environment[@]}" "${game_environment[@]}" \
    WINEDEBUG=-all \
    KUE_MODULE="$(windows_path "$module")" \
    KUE_CONFIG="$(windows_path "$KUE_CONFIG")" \
    KUE_LOG="$(windows_path "$KUE_LOG")" \
    GAME_PATTERN="$GAME_PATTERN" \
    "$wine" "$injector"
