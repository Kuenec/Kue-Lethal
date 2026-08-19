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

module_overridden=0
if [[ -v KUE_MODULE ]]; then
    if [[ -z "$KUE_MODULE" ]]; then
        printf '%s\n' '[kue] error: KUE_MODULE is set but empty' >&2
        exit 1
    fi
    module="$KUE_MODULE"
    module_overridden=1
else
    module="$project_root/build/kuelethal.so"
fi
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
maximum_fresh_log_bytes=$((1024 * 1024))

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        printf '[kue] error: required command is unavailable: %s\n' "$1" >&2
        exit 1
    fi
}

required_commands=(
    awk dd dirname file gdb grep iconv mkdir mktemp pgrep readelf readlink
    realpath rm sed sha256sum sleep stat strings tail tr
)
for command_name in "${required_commands[@]}"; do
    require_command "$command_name"
done

validate_text "$project_root" 4096 "project root"

gdb_command_file=""
cleanup() {
    local operation_status=$?
    trap - EXIT
    if [[ -n "$gdb_command_file" ]]; then
        if [[ -e "$gdb_command_file" || -L "$gdb_command_file" ]]; then
            if ! rm -f -- "$gdb_command_file"; then
                printf '%s%s\n' \
                    '[kue] error: cannot remove temporary GDB command file: ' \
                    "$gdb_command_file" >&2
                operation_status=1
            fi
        fi
    fi
    exit "$operation_status"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
validate_text "$module" 4096 "module path"
validate_text "$KUE_CONFIG" 4096 "configuration path"
validate_text "$KUE_LOG" 4096 "log path"
validate_text "$GAME_PATTERN" 256 "GAME_PATTERN"
if ((module_overridden != 0)) && [[ ! -f "$module" ]]; then
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

if [[ ! -f "$module" ]]; then
    printf '%s\n' '[kue] default module not found - building first'
    "$project_root/scripts/build.sh"
fi
if [[ ! -f "$module" || ! -r "$module" ]]; then
    printf '[kue] error: module is not a readable regular file: %s\n' \
        "$module" >&2
    exit 1
fi
if ! canonicalize_existing "$module" module; then
    printf '[kue] error: cannot resolve module: %s\n' "$module" >&2
    exit 1
fi
validate_text "$module" 4096 "resolved module path"

validate_module_architecture() {
    local module_description
    if ! module_description="$(LC_ALL=C file -Lb -- "$module")"; then
        printf '[kue] error: cannot inspect module: %s\n' "$module" >&2
        return 1
    fi
    case "$module_description" in
        "ELF 64-bit"*"shared object"*"x86-64"*) required_architecture=64 ;;
        *)
            printf '%s%s\n' \
                '[kue] error: module is not an x86-64 ELF shared object: ' \
                "$module_description" >&2
            return 1
            ;;
    esac
}

validate_module_architecture

read_build_identity() {
    LC_ALL=C strings -- "$module" | awk '
        /^KUE_BUILD_ID=/ {
            if (count == 0) value = $0
            ++count
        }
        END {
            if (count != 1) exit 1
            print value
        }
    '
}

if BUILD_ID="$(read_build_identity)"; then
    :
elif ((module_overridden != 0)); then
    printf '%s%s\n' \
        '[kue] error: configured module lacks one unique build identity: ' \
        "$module" >&2
    exit 1
else
    printf '%s\n' \
        '[kue] error: default module has no unique build identity; rebuilding'
    "$project_root/scripts/build.sh"
    if ! canonicalize_existing "$module" module; then
        printf '[kue] error: cannot resolve rebuilt module: %s\n' "$module" >&2
        exit 1
    fi
    validate_module_architecture
    if ! BUILD_ID="$(read_build_identity)"; then
        printf '%s\n' \
            '[kue] error: rebuilt module still lacks a unique build identity' \
            >&2
        exit 1
    fi
fi
MODULE_SHA="$(sha256sum "$module" | awk '{print $1}')"
echo "[kue] build inputs: $BUILD_ID"
echo "[kue] artifact sha256: $MODULE_SHA"

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
    printf '%s%s%s%s\n' \
        '[kue] error: no ' "$required_architecture" \
        "-bit x86-64 Unity/game process matching '$GAME_PATTERN' found " \
        "(candidates: ${candidate_pids[*]})"
    echo "[kue] hint: match it yourself with: GAME_PATTERN='<name>' $0"
    exit 1
fi

if [[ ! -r "/proc/$target/maps" ]]; then
    printf '[kue] error: cannot read mappings for selected pid %s\n' \
        "$target" >&2
    exit 1
fi

module_is_loaded() {
    local mapped_path target_mapped_path
    while IFS= read -r mapped_path; do
        mapped_path="${mapped_path% (deleted)}"
        if [[ "$mapped_path" == "$module" ]]; then
            return 0
        fi
        if [[ "$mapped_path" == /* ]]; then
            target_mapped_path="/proc/$target/root$mapped_path"
            if [[ -e "$target_mapped_path" ]]; then
                if [[ "$target_mapped_path" -ef "$module" ]]; then
                    return 0
                fi
            fi
        fi
    done < <(awk '
        {
            path = $0
            sub(/^[^ ]+ +[^ ]+ +[^ ]+ +[^ ]+ +[^ ]+ +/, "", path)
            if (path ~ /^\//) print path
        }
    ' "/proc/$target/maps")
    return 1
}

if module_is_loaded; then
    echo "[kue] error: Kue Lethal is already loaded in pid $target"
    echo '[kue] fully exit and restart the game before loading this rebuild'
    exit 1
fi

echo "[kue] injecting $module into pid $target (${required_architecture}-bit)"
if ! target_command="$(tr '\0' ' ' <"/proc/$target/cmdline")"; then
    echo "[kue] error: cannot read the selected target command line" >&2
    exit 1
fi
printf '%.200s\n' "$target_command"

libc_line="$(
    awk '$2 ~ /r--p/ && $3 == "00000000" && $NF ~ /\/libc\.so\.6$/ {
        print
        exit
    }' "/proc/$target/maps" 2>/dev/null || true
)"
libc_range="$(awk '{print $1}' <<<"$libc_line")"
libc_path="$(awk '{print $NF}' <<<"$libc_line")"
libc_base="${libc_range%%-*}"
target_libc="/proc/$target/root$libc_path"
if [[ -z "$libc_line" || ! -r "$target_libc" ]]; then
    echo "[kue] error: could not resolve the target process libc"
    exit 1
fi
read_versioned_function_offset() {
    local requested_symbol="$1"
    LC_ALL=C readelf -Ws "$target_libc" |
        awk -v requested="$requested_symbol" '
        $4 == "FUNC" && $8 ~ ("^" requested "@@") && !value { value=$2 }
        END { print value }
    '
}

dlopen_offset="$(read_versioned_function_offset dlopen)"
dlclose_offset="$(read_versioned_function_offset dlclose)"
dlerror_offset="$(read_versioned_function_offset dlerror)"
dlsym_offset="$(read_versioned_function_offset dlsym)"
errno_location_offset="$(read_versioned_function_offset __errno_location)"
free_offset="$(read_versioned_function_offset free)"
getenv_offset="$(read_versioned_function_offset getenv)"
setenv_offset="$(read_versioned_function_offset setenv)"
strdup_offset="$(read_versioned_function_offset strdup)"
unsetenv_offset="$(read_versioned_function_offset unsetenv)"
missing_loader_export=0
for loader_offset in "$dlopen_offset" "$dlclose_offset" "$dlerror_offset" \
    "$dlsym_offset" "$errno_location_offset" "$free_offset" \
    "$getenv_offset" "$setenv_offset" "$strdup_offset" "$unsetenv_offset"; do
    if [[ -z "$loader_offset" ]]; then
        missing_loader_export=1
    fi
done
if ((missing_loader_export != 0)); then
    echo '[kue] error: target libc lacks a required loader/environment export'
    exit 1
fi
REAL_DLOPEN="$(printf '0x%x' "$((16#$libc_base + 16#$dlopen_offset))")"
REAL_DLCLOSE="$(printf '0x%x' "$((16#$libc_base + 16#$dlclose_offset))")"
REAL_DLERROR="$(printf '0x%x' "$((16#$libc_base + 16#$dlerror_offset))")"
REAL_DLSYM="$(printf '0x%x' "$((16#$libc_base + 16#$dlsym_offset))")"
REAL_ERRNO_LOCATION="$(
    printf '0x%x' "$((16#$libc_base + 16#$errno_location_offset))"
)"
REAL_FREE="$(printf '0x%x' "$((16#$libc_base + 16#$free_offset))")"
REAL_GETENV="$(printf '0x%x' "$((16#$libc_base + 16#$getenv_offset))")"
REAL_SETENV="$(printf '0x%x' "$((16#$libc_base + 16#$setenv_offset))")"
REAL_STRDUP="$(printf '0x%x' "$((16#$libc_base + 16#$strdup_offset))")"
REAL_UNSETENV="$(printf '0x%x' "$((16#$libc_base + 16#$unsetenv_offset))")"

log_start_bytes=0
log_start_identity=""
if [[ -f "$KUE_LOG" ]]; then
    if ! log_start_bytes="$(stat -c %s "$KUE_LOG")"; then
        printf '[kue] error: cannot inspect configured log: %s\n' \
            "$KUE_LOG" >&2
        exit 1
    fi
    if ! log_start_identity="$(stat -Lc '%d:%i' "$KUE_LOG")"; then
        printf '[kue] error: cannot identify configured log: %s\n' \
            "$KUE_LOG" >&2
        exit 1
    fi
fi

gdb_escape() {
    local value="$1"
    if [[ "$value" == *$'\n'* || "$value" == *$'\r'* ]]; then
        echo "[kue] error: injection paths cannot contain line breaks" >&2
        exit 1
    fi
    value="${value//\\/\\\\}"
    value="${value//\"/\\\"}"
    printf '%s' "$value"
}

module_gdb="$(gdb_escape "$module")"
config_gdb="$(gdb_escape "$KUE_CONFIG")"
log_gdb="$(gdb_escape "$KUE_LOG")"

write_gdb_commands() {
    local output_path="$1"
    local bind_setenv config_length_loop config_byte_test
    local config_too_long config_copy_failed log_length_loop log_byte_test
    local log_too_long log_copy_failed config_set config_set_failed
    local log_set log_set_failed null_start start_ok config_restore
    local log_restore rollback_failed close_failed
    bind_setenv='set $real_setenv = '
    bind_setenv+='(int(*)(const char*, const char*, int))'
    bind_setenv+="$REAL_SETENV"
    config_length_loop='  while $previous_config_length <= 4096'
    config_length_loop+=' && $previous_config_terminated == 0'
    config_byte_test='    if *(unsigned char*)($previous_config_value'
    config_byte_test+=' + $previous_config_length) == 0'
    config_too_long='    printf "target environment capture failed: '
    config_too_long+='KUE_CONFIG exceeds 4096 bytes\n"'
    config_copy_failed='      printf "target environment capture failed: '
    config_copy_failed+='cannot copy KUE_CONFIG, errno=%d\n", $capture_errno'
    log_length_loop='  while $previous_log_length <= 4096'
    log_length_loop+=' && $previous_log_terminated == 0'
    log_byte_test='    if *(unsigned char*)($previous_log_value'
    log_byte_test+=' + $previous_log_length) == 0'
    log_too_long='    printf "target environment capture failed: '
    log_too_long+='KUE_LOG exceeds 4096 bytes\n"'
    log_copy_failed='      printf "target environment capture failed: '
    log_copy_failed+='cannot copy KUE_LOG, errno=%d\n", $capture_errno'
    config_set='  set $config_result = $real_setenv("KUE_CONFIG", "'
    config_set+="$config_gdb"
    config_set+='", 1)'
    config_set_failed='    printf "target setenv failed: KUE_CONFIG errno=%d, '
    config_set_failed+='KUE_LOG not attempted\n", $config_errno'
    log_set='    set $log_result = $real_setenv("KUE_LOG", "'
    log_set+="$log_gdb"
    log_set+='", 1)'
    log_set_failed='      printf "target setenv failed: KUE_CONFIG changed, '
    log_set_failed+='KUE_LOG errno=%d\n", $log_errno'
    null_start='            printf "dlsym kue_start returned null '
    null_start+='without dlerror\n"'
    start_ok='              printf "kue_start ok: handle=%p symbol=%p\n", '
    start_ok+='$h, $start'
    config_restore='      set $config_restore_result = '
    config_restore+='$real_setenv("KUE_CONFIG", $previous_config, 1)'
    log_restore='      set $log_restore_result = '
    log_restore+='$real_setenv("KUE_LOG", $previous_log, 1)'
    rollback_failed='      printf "target environment rollback failed: '
    rollback_failed+='KUE_CONFIG errno=%d, KUE_LOG errno=%d\n", '
    rollback_failed+='$config_restore_errno, $log_restore_errno'
    close_failed='      printf "dlclose after startup failure failed: %s\n", '
    close_failed+='$real_dlerror()'
    printf '%s\n' \
        'printf "kue target transaction entered\n"' \
        'set confirm off' \
        'set pagination off' \
        'set debuginfod enabled off' \
        'thread 1' \
        'set $real_dlopen = (void*(*)(const char*, int))'"$REAL_DLOPEN" \
        'set $real_dlclose = (int(*)(void*))'"$REAL_DLCLOSE" \
        'set $real_dlerror = (char*(*)(void))'"$REAL_DLERROR" \
        'set $real_dlsym = (void*(*)(void*, const char*))'"$REAL_DLSYM" \
        'set $real_errno_location = (int*(*)(void))'"$REAL_ERRNO_LOCATION" \
        'set $real_free = (void(*)(void*))'"$REAL_FREE" \
        'set $real_getenv = (char*(*)(const char*))'"$REAL_GETENV" \
        "$bind_setenv" \
        'set $real_strdup = (char*(*)(const char*))'"$REAL_STRDUP" \
        'set $real_unsetenv = (int(*)(const char*))'"$REAL_UNSETENV" \
        'call $real_dlerror()' \
        'set $previous_config_value = $real_getenv("KUE_CONFIG")' \
        'set $previous_log_value = $real_getenv("KUE_LOG")' \
        'set $previous_config_was_set = $previous_config_value != 0' \
        'set $previous_log_was_set = $previous_log_value != 0' \
        'set $previous_config = (char*)0' \
        'set $previous_log = (char*)0' \
        'set $environment_ready = 1' \
        'set $environment_changed = 0' \
        'set $h = (void*)0' \
        'set $retain_handle = 0' \
        'if $previous_config_was_set != 0' \
        '  set $previous_config_length = 0' \
        '  set $previous_config_terminated = 0' \
        "$config_length_loop" \
        "$config_byte_test" \
        '      set $previous_config_terminated = 1' \
        '    else' \
        '      set $previous_config_length = $previous_config_length + 1' \
        '    end' \
        '  end' \
        '  if $previous_config_terminated == 0' \
        '    set $environment_ready = 0' \
        "$config_too_long" \
        '  else' \
        '    set $previous_config = $real_strdup($previous_config_value)' \
        '    if $previous_config == 0' \
        '      set $environment_ready = 0' \
        '      set $capture_errno = *$real_errno_location()' \
        "$config_copy_failed" \
        '    end' \
        '  end' \
        'end' \
        'if $previous_log_was_set != 0' \
        '  set $previous_log_length = 0' \
        '  set $previous_log_terminated = 0' \
        "$log_length_loop" \
        "$log_byte_test" \
        '      set $previous_log_terminated = 1' \
        '    else' \
        '      set $previous_log_length = $previous_log_length + 1' \
        '    end' \
        '  end' \
        '  if $previous_log_terminated == 0' \
        '    set $environment_ready = 0' \
        "$log_too_long" \
        '  else' \
        '    set $previous_log = $real_strdup($previous_log_value)' \
        '    if $previous_log == 0' \
        '      set $environment_ready = 0' \
        '      set $capture_errno = *$real_errno_location()' \
        "$log_copy_failed" \
        '    end' \
        '  end' \
        'end' \
        'if $environment_ready != 0' \
        "$config_set" \
        '  if $config_result != 0' \
        '    set $config_errno = *$real_errno_location()' \
        "$config_set_failed" \
        '  else' \
        '    set $environment_changed = 1' \
        "$log_set" \
        '    if $log_result != 0' \
        '      set $log_errno = *$real_errno_location()' \
        "$log_set_failed" \
        '    else' \
        '      set $h = $real_dlopen("'"$module_gdb"'", 2)' \
        '      if $h == 0' \
        '        printf "dlopen failed: %s\n", $real_dlerror()' \
        '      else' \
        '        call $real_dlerror()' \
        '        set $start = $real_dlsym($h, "kue_start")' \
        '        set $symbol_error = $real_dlerror()' \
        '        if $symbol_error != 0' \
        '          printf "dlsym kue_start failed: %s\n", $symbol_error' \
        '        else' \
        '          if $start == 0' \
        "$null_start" \
        '          else' \
        '            set $start_result = ((int(*)(void))$start)()' \
        '            if $start_result == 0' \
        '              set $retain_handle = 1' \
        "$start_ok" \
        '            else' \
        '              printf "kue_start failed: result=%d\n", $start_result' \
        '            end' \
        '          end' \
        '        end' \
        '      end' \
        '    end' \
        '  end' \
        'end' \
        'if $retain_handle == 0' \
        '  if $environment_changed != 0' \
        '    set $config_restore_result = 0' \
        '    set $config_restore_errno = 0' \
        '    set $log_restore_result = 0' \
        '    set $log_restore_errno = 0' \
        '    if $previous_config_was_set != 0' \
        "$config_restore" \
        '    else' \
        '      set $config_restore_result = $real_unsetenv("KUE_CONFIG")' \
        '    end' \
        '    if $config_restore_result != 0' \
        '      set $config_restore_errno = *$real_errno_location()' \
        '    end' \
        '    if $previous_log_was_set != 0' \
        "$log_restore" \
        '    else' \
        '      set $log_restore_result = $real_unsetenv("KUE_LOG")' \
        '    end' \
        '    if $log_restore_result != 0' \
        '      set $log_restore_errno = *$real_errno_location()' \
        '    end' \
        '    if $config_restore_result != 0 || $log_restore_result != 0' \
        "$rollback_failed" \
        '    else' \
        '      printf "failed startup target environment restored\n"' \
        '    end' \
        '  end' \
        '  if $h != 0' \
        '    call $real_dlerror()' \
        '    set $close_result = $real_dlclose($h)' \
        '    if $close_result != 0' \
        "$close_failed" \
        '    else' \
        '      printf "failed startup module handle released\n"' \
        '    end' \
        '  end' \
        'end' \
        'if $previous_config != 0' \
        '  call $real_free($previous_config)' \
        'end' \
        'if $previous_log != 0' \
        '  call $real_free($previous_log)' \
        'end' \
        'detach' \
        'quit' >"$output_path"
}

is_pre_mutation_permission_failure() {
    local attach_status="$1"
    local attach_output="$2"
    if ((attach_status == 0)); then
        return 1
    fi
    if [[ "$attach_output" != *'ptrace: Operation not permitted.'* ]]; then
        return 1
    fi
    [[ "$attach_output" != *'kue target transaction entered'* ]]
}

run_gdb() {
    local target_pid="$1"
    shift
    local gdb_status=0
    if ! gdb_command_file="$(mktemp -t kue-inject-gdb.XXXXXX)"; then
        printf '%s\n' \
            '[kue] error: cannot create temporary GDB command file' >&2
        return 1
    fi
    if ! write_gdb_commands "$gdb_command_file"; then
        printf '[kue] error: cannot write temporary GDB command file: %s\n' \
            "$gdb_command_file" >&2
        return 1
    fi
    if "$@" gdb -q -p "$target_pid" -batch \
        -x "$gdb_command_file" </dev/null 2>&1; then
        gdb_status=0
    else
        gdb_status=$?
    fi
    if ! rm -f -- "$gdb_command_file"; then
        printf '[kue] error: cannot remove temporary GDB command file: %s\n' \
            "$gdb_command_file" >&2
        return 1
    fi
    gdb_command_file=""
    return "$gdb_status"
}

log_armed() {
    local fresh
    if ! fresh="$(fresh_log)"; then
        return 2
    fi
    grep -Fq "$BUILD_ID" <<<"$fresh" &&
        grep -q "main-thread HUD installer armed" <<<"$fresh"
}

fresh_log() {
    local current_identity current_size fresh_bytes
    if [[ ! -f "$KUE_LOG" ]]; then
        return 0
    fi
    if [[ -n "$log_start_identity" ]]; then
        if ! current_identity="$(stat -Lc '%d:%i' "$KUE_LOG")"; then
            printf '[kue] error: cannot identify configured log: %s\n' \
                "$KUE_LOG" >&2
            return 1
        fi
        if [[ "$current_identity" != "$log_start_identity" ]]; then
            printf '%s%s\n' \
                '[kue] error: configured log replaced during injection: ' \
                "$KUE_LOG" >&2
            return 1
        fi
    fi
    if ! current_size="$(stat -c %s "$KUE_LOG")"; then
        printf '[kue] error: cannot inspect configured log: %s\n' \
            "$KUE_LOG" >&2
        return 1
    fi
    if ((current_size < log_start_bytes)); then
        printf '%s%s\n' \
            '[kue] error: configured log truncated during injection: ' \
            "$KUE_LOG" >&2
        return 1
    fi
    fresh_bytes=$((current_size - log_start_bytes))
    if ((fresh_bytes > maximum_fresh_log_bytes)); then
        printf '[kue] error: fresh log data exceeds %d bytes: %s\n' \
            "$maximum_fresh_log_bytes" "$KUE_LOG" >&2
        return 1
    fi
    if ((fresh_bytes != 0)); then
        dd if="$KUE_LOG" iflag=skip_bytes,count_bytes skip="$log_start_bytes" \
            count="$fresh_bytes" status=none
    fi
}

report_runtime_result() {
    local attempt fresh
    local installed_pattern exhausted_pattern arm_pattern
    installed_pattern='late HUD installer attempt|late HUD installer active'
    installed_pattern+='|internal Unity HUD installed'
    exhausted_pattern='late HUD installer attempt|mono: target method'
    exhausted_pattern+='|mono: compiled Update|mono: could not allocate'
    exhausted_pattern+='|mono: could not make'
    arm_pattern='late HUD installer attempt|mono: target method'
    arm_pattern+='|main-thread HUD installer armed'
    for ((attempt = 1; attempt <= 60; ++attempt)); do
        if ! fresh="$(fresh_log)"; then
            return 1
        fi
        if grep -q 'internal Unity HUD installed' <<<"$fresh"; then
            echo "[kue] HUD delivery hook confirmed"
            grep -E "$installed_pattern" <<<"$fresh" | tail -n 10
            return 0
        fi
        if grep -q 'late HUD installer attempt 5/5' <<<"$fresh"; then
            echo '[kue] identity matched, but the live hook exhausted retries:'
            grep -E "$exhausted_pattern" <<<"$fresh" | tail -n 14
            return 1
        fi
        sleep 0.25
    done
    if ! fresh="$(fresh_log)"; then
        return 1
    fi
    grep -E "$arm_pattern" <<<"$fresh" | tail -n 10
    echo "[kue] error: HUD delivery was not confirmed within 15 seconds" >&2
    echo '[kue] module remains loaded; fully exit the game before retrying' >&2
    return 1
}

wait_for_arm() {
    local attempt arm_status
    for ((attempt = 1; attempt <= 40; ++attempt)); do
        if log_armed; then
            return 0
        else
            arm_status=$?
        fi
        if ((arm_status == 2)); then
            return 2
        fi
        sleep 0.25
    done
    return 1
}

out="$(run_gdb "$target")" && gdb_status=0 || gdb_status=$?
if grep -q 'kue_start ok' <<<"$out"; then
    if ((gdb_status != 0)); then
        printf '[kue] module started, but cleanup failed (gdb status %d)\n' \
            "$gdb_status" >&2
        printf '%s\n' "$out" >&2
        echo '[kue] exit before retrying; a second attach is unsafe' >&2
        exit 1
    fi
    if wait_for_arm; then
        printf '[kue] injection complete - identity confirmed (log: %s)\n' \
            "$KUE_LOG"
        echo '[kue] HUD should appear after the late Update hook runs'
        report_runtime_result
        exit $?
    else
        arm_status=$?
    fi
    if ((arm_status == 2)); then
        exit 1
    fi
    echo '[kue] module started but runtime readiness was not confirmed'
    echo "[kue] no matching loader trace appeared in $KUE_LOG"
    echo '[kue] exit the game before retrying; no second copy will run'
    exit 1
fi
operation_failure_pattern='dlopen failed\|dlsym kue_start failed'
operation_failure_pattern+='\|dlsym kue_start returned null\|kue_start failed'
operation_failure_pattern+='\|target environment capture failed'
operation_failure_pattern+='\|target environment rollback failed'
operation_failure_pattern+='\|target setenv failed'
if grep -q "$operation_failure_pattern" <<<"$out"; then
    echo "[kue] target operation failed:" >&2
    printf '%s\n' "$out" >&2
    exit 1
fi
if ! is_pre_mutation_permission_failure "$gdb_status" "$out"; then
    printf '%s%d%s\n' \
        '[kue] unprivileged attach failed unclassified (gdb status ' \
        "$gdb_status" ')' >&2
    printf '%s\n' "$out" >&2
    echo '[kue] exit the game before retrying; target state is unknown' >&2
    exit 1
fi
printf '[kue] unprivileged attach failed (gdb status %d); trying root\n' \
    "$gdb_status"
printf '%s\n' "$out" >&2
require_command sudo
out="$(run_gdb "$target" sudo -n)" && root_gdb_status=0 || root_gdb_status=$?

if grep -q 'kue_start ok' <<<"$out"; then
    if ((root_gdb_status != 0)); then
        printf '[kue] module started; root cleanup failed (gdb status %d)\n' \
            "$root_gdb_status" >&2
        printf '%s\n' "$out" >&2
        echo '[kue] exit before retrying; a second attach is unsafe' >&2
        exit 1
    fi
    if wait_for_arm; then
        printf '[kue] injection complete (root); identity log: %s\n' \
            "$KUE_LOG"
        echo '[kue] HUD should appear after the late Update hook runs'
        report_runtime_result
        exit $?
    else
        arm_status=$?
    fi
    if ((arm_status == 2)); then
        exit 1
    fi
fi
echo "[kue] injection FAILED (root gdb status $root_gdb_status):"
printf '%s\n' "$out" >&2
if grep -q 'kue_start ok' <<<"$out"; then
    echo "[kue] no matching loader trace appeared in $KUE_LOG"
fi
echo '[kue] no HUD loading route exists when ptrace attachment is prohibited'
exit 1
