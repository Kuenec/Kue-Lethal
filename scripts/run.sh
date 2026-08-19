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

entry_validation="$project_root/scripts/entry-validation.sh"
if [[ ! -f "$entry_validation" || ! -r "$entry_validation" ]]; then
    printf 'error: entry validation is unavailable: %s\n' \
        "$entry_validation" >&2
    exit 1
fi
source "$entry_validation"

for command_name in dirname file iconv mkdir realpath; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        printf 'error: required command is unavailable: %s\n' \
            "$command_name" >&2
        exit 1
    fi
done

validate_text "$project_root" 4096 "project root"

if [[ -v KUE_MODULE && -z "$KUE_MODULE" ]]; then
    printf '%s\n' 'error: KUE_MODULE is set but empty' >&2
    exit 1
fi
module="${KUE_MODULE-$project_root/build/kuelethal.so}"
validate_text "$module" 4096 "module path"
if [[ ! -f "$module" || ! -r "$module" ]]; then
    printf 'error: module not found: %s\n' "$module" >&2
    printf '%s\n' 'build it first with scripts/build.sh' >&2
    exit 1
fi
if ! canonicalize_existing "$module" module; then
    printf 'error: cannot resolve module: %s\n' "$module" >&2
    exit 1
fi
validate_text "$module" 4096 "resolved module path"
if ! module_description="$(LC_ALL=C file -Lb -- "$module")"; then
    printf 'error: cannot inspect module: %s\n' "$module" >&2
    exit 1
fi
case "$module_description" in
    "ELF 64-bit"*"shared object"*"x86-64"*) ;;
    *)
        printf 'error: module is not an x86-64 ELF shared object: %s\n' \
            "$module_description" >&2
        exit 1
        ;;
esac
if [[ "$module" == *:* || "$module" == *[[:space:]]* ]]; then
    printf 'error: module path contains an LD_PRELOAD separator: %s\n' \
        "$module" >&2
    exit 1
fi
if (($# == 0)); then
    printf '%s\n' 'usage: scripts/run.sh GAME_COMMAND [ARGUMENT ...]' >&2
    exit 1
fi

if [[ -v KUE_CONFIG && -z "$KUE_CONFIG" ]]; then
    printf '%s\n' 'error: KUE_CONFIG is set but empty' >&2
    exit 1
fi
if [[ -v KUE_LOG && -z "$KUE_LOG" ]]; then
    printf '%s\n' 'error: KUE_LOG is set but empty' >&2
    exit 1
fi
if [[ ! -v KUE_LOG && -v XDG_STATE_HOME && -z "$XDG_STATE_HOME" ]]; then
    printf '%s\n' \
        'error: XDG_STATE_HOME is set but empty; set KUE_LOG explicitly' >&2
    exit 1
fi
if [[ ! -v KUE_LOG && ! -v XDG_STATE_HOME && -z "${HOME:-}" ]]; then
    printf '%s\n' \
        'error: HOME and XDG_STATE_HOME are unset; set KUE_LOG explicitly' >&2
    exit 1
fi
state_home="${XDG_STATE_HOME-${HOME:-}/.local/state}"
config_path="${KUE_CONFIG-$project_root/config/kuelethal.json}"
log_path="${KUE_LOG-$state_home/kuelethal/kuelethal.log}"
validate_text "$config_path" 4096 "configuration path"
validate_text "$log_path" 4096 "log path"
if [[ ! -f "$config_path" || ! -r "$config_path" ]]; then
    printf 'error: configuration file not found: %s\n' "$config_path" >&2
    exit 1
fi
if ! canonicalize_existing "$config_path" config_path; then
    printf 'error: cannot resolve configuration file: %s\n' "$config_path" >&2
    exit 1
fi
validate_text "$config_path" 4096 "resolved configuration path"
log_name="${log_path##*/}"
if [[ -z "$log_name" || "$log_name" == "." || "$log_name" == ".." ]]; then
    printf 'error: log path does not name a file: %s\n' "$log_path" >&2
    exit 1
fi
log_directory="$(dirname -- "$log_path")"
if ! mkdir -p -- "$log_directory"; then
    printf 'error: cannot create log directory: %s\n' "$log_directory" >&2
    exit 1
fi
if ! canonicalize_existing "$log_directory" log_directory; then
    printf 'error: cannot resolve log directory: %s\n' "$log_directory" >&2
    exit 1
fi
if [[ "$log_directory" == "/" ]]; then
    log_path="/$log_name"
else
    log_path="$log_directory/$log_name"
fi
validate_text "$log_path" 4096 "resolved log path"
if [[ (-e "$log_path" || -L "$log_path") && ! -f "$log_path" ]]; then
    printf 'error: log target is not a regular file: %s\n' "$log_path" >&2
    exit 1
fi
if [[ -e "$log_path" ]]; then
    if [[ ! -w "$log_path" ]]; then
        printf 'error: log target is not writable: %s\n' "$log_path" >&2
        exit 1
    fi
    if ! canonicalize_existing "$log_path" log_path; then
        printf 'error: cannot resolve log target: %s\n' "$log_path" >&2
        exit 1
    fi
    validate_text "$log_path" 4096 "resolved log path"
elif [[ ! -w "$log_directory" ]]; then
    printf 'error: log directory is not writable: %s\n' "$log_directory" >&2
    exit 1
fi
if [[ -v KUE_VERBOSE && "$KUE_VERBOSE" != "0" && "$KUE_VERBOSE" != "1" ]]; then
    printf 'error: KUE_VERBOSE must be 0 or 1, got: %s\n' "$KUE_VERBOSE" >&2
    exit 1
fi
export KUE_CONFIG="$config_path"
export KUE_LOG="$log_path"
if [[ "${KUE_VERBOSE-0}" == "1" ]]; then
    export KUE_CONSOLE=1
fi
export LD_PRELOAD="$module${LD_PRELOAD:+:$LD_PRELOAD}"
printf '[kue] loading %s into: %s\n' "$module" "$1"
exec "$@"
