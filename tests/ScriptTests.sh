#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_directory="$(mktemp -d)"
checks=0
failures=0
command_status=0
selection_worker=""

cleanup() {
    local operation_status=$?
    trap - EXIT
    if [[ -n "$selection_worker" ]]; then
        if kill -0 "$selection_worker" 2>/dev/null; then
            if ! kill "$selection_worker"; then
                printf 'FAIL: cannot stop target fixture: %s\n' \
                    "$selection_worker" >&2
                operation_status=1
            fi
        fi
        if wait "$selection_worker" 2>/dev/null; then
            :
        fi
        selection_worker=""
    fi
    if [[ -d "$test_directory" ]]; then
        if ! find "$test_directory" -depth -delete; then
            printf 'FAIL: cannot remove script-test directory: %s\n' \
                "$test_directory" >&2
            operation_status=1
        fi
    fi
    exit "$operation_status"
}

trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

run_command() {
    if "$@"; then
        command_status=0
    else
        command_status=$?
    fi
}

expect_failure() {
    local status="$1"
    local name="$2"
    checks=$((checks + 1))
    if ((status == 0)); then
        printf 'FAIL: %s\n' "$name" >&2
        failures=$((failures + 1))
    fi
}

expect_success() {
    local status="$1"
    local name="$2"
    checks=$((checks + 1))
    if ((status != 0)); then
        printf 'FAIL: %s\n' "$name" >&2
        failures=$((failures + 1))
    fi
}

expect_absent() {
    local path="$1"
    local name="$2"
    checks=$((checks + 1))
    if [[ -e "$path" || -L "$path" ]]; then
        printf 'FAIL: %s\n' "$name" >&2
        failures=$((failures + 1))
    fi
}

expect_line() {
    local expected="$1"
    local path="$2"
    local name="$3"
    checks=$((checks + 1))
    if ! grep -Fxq -- "$expected" "$path"; then
        printf 'FAIL: %s\n' "$name" >&2
        failures=$((failures + 1))
    fi
}

expect_contains() {
    local expected="$1"
    local path="$2"
    local name="$3"
    checks=$((checks + 1))
    if ! grep -Fq -- "$expected" "$path"; then
        printf 'FAIL: %s\n' "$name" >&2
        failures=$((failures + 1))
    fi
}

expect_count() {
    local expected="$1"
    local text="$2"
    local path="$3"
    local name="$4"
    local actual
    actual="$(grep -Fc -- "$text" "$path" || true)"
    checks=$((checks + 1))
    if [[ "$actual" != "$expected" ]]; then
        printf 'FAIL: %s (expected %s, got %s)\n' \
            "$name" "$expected" "$actual" >&2
        failures=$((failures + 1))
    fi
}

read_object_build_identity() {
    local build_directory="$1"
    local compiler_path="$2"
    local -a objects=()
    mapfile -t objects < <(
        find "$build_directory" -type f \
            -path '*/CMakeFiles/kue-build-identity.dir/*' -name '*.o' \
            -print
    )
    if ((${#objects[@]} != 1)); then
        printf 'FAIL: expected one identity object, found %d\n' \
            "${#objects[@]}" >&2
        return 1
    fi
    "$compiler_path" -flto -shared "${objects[0]}" \
        -o "$build_directory/build-identity.so"
    LC_ALL=C strings -- "$build_directory/build-identity.so" | awk '
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

configure_identity_fixture() {
    local source_directory="$1"
    local build_directory="$2"
    local compiler_path="$3"
    local configuration="$4"
    cmake -S "$source_directory" -B "$build_directory" -G Ninja \
        -DBUILD_TESTING=OFF -DKUE_BUILD_MODULE=OFF -DKUE_FETCH_DEPS=OFF \
        -DCMAKE_CXX_COMPILER="$compiler_path" \
        -DCMAKE_BUILD_TYPE="$configuration" \
        >/dev/null
    cmake --build "$build_directory" --target kue-build-identity >/dev/null
}

run_build_identity_contracts() {
    local compiler_path alternate_compiler
    local first_identity relocated_identity changed_identity
    local debug_identity alternate_identity
    local -a identity_inputs
    compiler_path="$(command -v "${CXX:-c++}")"
    if "$compiler_path" --version | grep -Fqi clang; then
        alternate_compiler="$(command -v g++)"
    else
        alternate_compiler="$(command -v clang++)"
    fi
    mkdir "$test_directory/source-a" "$test_directory/source-b"
    identity_inputs=(
        "$project_root/CMakeLists.txt" "$project_root/cmake"
        "$project_root/config" "$project_root/scripts" "$project_root/src"
    )
    cp -a -- "${identity_inputs[@]}" "$test_directory/source-a/"
    cp -a -- "${identity_inputs[@]}" "$test_directory/source-b/"

    configure_identity_fixture \
        "$test_directory/source-a" "$test_directory/build-a" \
        "$compiler_path" Release
    first_identity="$(
        read_object_build_identity "$test_directory/build-a" "$compiler_path"
    )"
    configure_identity_fixture \
        "$test_directory/source-b" "$test_directory/build-b" \
        "$compiler_path" Release
    relocated_identity="$(
        read_object_build_identity "$test_directory/build-b" "$compiler_path"
    )"
    checks=$((checks + 1))
    if [[ "$first_identity" != "$relocated_identity" ]]; then
        printf '%s\n' \
            'FAIL: identical inputs have a path-independent identity' >&2
        failures=$((failures + 1))
    fi

    printf '\n' >>"$test_directory/source-b/config/kuelethal.json"
    cmake --build "$test_directory/build-b" \
        --target kue-build-identity >/dev/null
    changed_identity="$(
        read_object_build_identity "$test_directory/build-b" "$compiler_path"
    )"
    checks=$((checks + 1))
    if [[ "$first_identity" == "$changed_identity" ]]; then
        printf '%s\n' \
            'FAIL: a changed production input changes the identity' >&2
        failures=$((failures + 1))
    fi

    configure_identity_fixture \
        "$test_directory/source-a" "$test_directory/build-debug" \
        "$compiler_path" Debug
    debug_identity="$(
        read_object_build_identity \
            "$test_directory/build-debug" "$compiler_path"
    )"
    checks=$((checks + 1))
    if [[ "$first_identity" == "$debug_identity" ]]; then
        printf '%s\n' \
            'FAIL: a changed build configuration changes the identity' >&2
        failures=$((failures + 1))
    fi

    configure_identity_fixture \
        "$test_directory/source-a" "$test_directory/build-alternate" \
        "$alternate_compiler" Release
    alternate_identity="$(
        read_object_build_identity "$test_directory/build-alternate" \
            "$alternate_compiler"
    )"
    checks=$((checks + 1))
    if [[ "$first_identity" == "$alternate_identity" ]]; then
        printf '%s\n' \
            'FAIL: a changed compiler toolchain changes the identity' >&2
        failures=$((failures + 1))
    fi

    if ((failures == 0)); then
        printf 'build identity tests passed: %d checks\n' "$checks"
        return 0
    fi
    printf 'build identity tests failed: %d of %d checks\n' \
        "$failures" "$checks" >&2
    return 1
}

run_entry_validation_contracts() {
    local result status
    source "$project_root/scripts/entry-validation.sh"

    if validate_text 'é' 2 'multibyte value' \
        >"$test_directory/entry-output" 2>"$test_directory/entry-error"; then
        status=0
    else
        status=$?
    fi
    expect_success "$status" "entry validation measures UTF-8 in bytes"

    iconv() {
        return 88
    }
    if validate_text 'plain/ascii/path' 32 'ASCII value'; then
        status=0
    else
        status=$?
    fi
    unset -f iconv
    expect_success "$status" \
        "entry validation accepts ASCII without conversion"

    if validate_text $'bad\nvalue' 32 'control value' \
        >"$test_directory/entry-output" 2>"$test_directory/entry-error"; then
        status=0
    else
        status=$?
    fi
    expect_failure "$status" "entry validation rejects control bytes"
    expect_line 'error: control value exceeds its text contract' \
        "$test_directory/entry-error" \
        "entry validation reports the control contract"

    if validate_text $'\xff' 32 'encoded value' \
        >"$test_directory/entry-output" 2>"$test_directory/entry-error"; then
        status=0
    else
        status=$?
    fi
    expect_failure "$status" "entry validation rejects ill-formed UTF-8"
    expect_line 'error: encoded value must be valid UTF-8' \
        "$test_directory/entry-error" \
        "entry validation reports the encoding contract"

    mkdir "$test_directory/entry-canonical"$'\n'
    ln -s "entry-canonical"$'\n' "$test_directory/entry-canonical-link"
    result='unchanged'
    if canonicalize_existing \
        "$test_directory/entry-canonical-link" result; then
        status=0
    else
        status=$?
    fi
    expect_success "$status" "entry validation canonicalizes an existing path"
    checks=$((checks + 1))
    if [[ "$result" != "$test_directory/entry-canonical"$'\n' ]]; then
        printf '%s\n' \
            'FAIL: entry validation preserves canonical path bytes' >&2
        failures=$((failures + 1))
    fi
}

run_process_search_contracts() {
    local collector_source status
    collector_source="$(
        sed -n '/^collect_candidate_pids() {/,/^}/p' \
            "$project_root/scripts/inject.sh"
    )"
    checks=$((checks + 1))
    if [[ -z "$collector_source" ]]; then
        printf '%s\n' \
            'FAIL: inject exposes one bounded candidate collector' >&2
        failures=$((failures + 1))
        return
    fi
    eval "$collector_source"
    GAME_PATTERN='fixture-game'

    process_search_fixture=exact
    pgrep() {
        case "$process_search_fixture" in
            exact) printf '%s\n' 101 202 ;;
            none) return 1 ;;
            error) return 3 ;;
            overflow)
                local candidate
                for ((candidate = 1; candidate <= 257; ++candidate)); do
                    printf '%d\n' "$candidate"
                done
                ;;
        esac
    }

    if collect_candidate_pids >"$test_directory/process-search-output" \
        2>"$test_directory/process-search-error"; then
        status=0
    else
        status=$?
    fi
    expect_success "$status" "inject collects an exact bounded candidate array"
    checks=$((checks + 1))
    if ((${#candidate_pids[@]} != 2)) || [[ "${candidate_pids[0]}" != 101 ]] ||
        [[ "${candidate_pids[1]}" != 202 ]]; then
        printf '%s\n' \
            'FAIL: inject preserves candidate boundaries and order' >&2
        failures=$((failures + 1))
    fi

    process_search_fixture=none
    if collect_candidate_pids >"$test_directory/process-search-output" \
        2>"$test_directory/process-search-error"; then
        status=0
    else
        status=$?
    fi
    checks=$((checks + 1))
    if ((status != 1)) || ((${#candidate_pids[@]} != 0)); then
        printf '%s\n' 'FAIL: inject distinguishes an empty process search' >&2
        failures=$((failures + 1))
    fi

    process_search_fixture=error
    if collect_candidate_pids >"$test_directory/process-search-output" \
        2>"$test_directory/process-search-error"; then
        status=0
    else
        status=$?
    fi
    expect_failure "$status" "inject propagates a process-search failure"
    expect_contains 'process search failed for pattern: fixture-game' \
        "$test_directory/process-search-error" \
        "inject reports the failed process search"

    process_search_fixture=overflow
    if collect_candidate_pids >"$test_directory/process-search-output" \
        2>"$test_directory/process-search-error"; then
        status=0
    else
        status=$?
    fi
    expect_failure "$status" "inject rejects more than 256 process candidates"
    expect_contains 'process search exceeded 256 candidates' \
        "$test_directory/process-search-error" \
        "inject reports the candidate capacity"
}

enum_members() {
    local path="$1"
    local declaration="$2"
    awk -v declaration="$declaration" '
        index($0, declaration) { inside = 1; next }
        inside && /}/ { exit }
        inside {
            gsub(/[[:space:]]|,|=.*/, "")
            if ($0 != "" && $0 != "{" && $0 != "Count") print
        }
    ' "$path"
}

run_bridge_enum_contracts() {
    local managed="$project_root/src/managed/KueInternalHud.cs"
    local enum_name
    local native
    for enum_name in PlayerAction SessionSwitch WorldFeature; do
        native="$project_root/src/game/Game.h"
        if [[ "$enum_name" == "PlayerAction" ]]; then
            native="$project_root/src/game/PlayerActions.h"
        fi
        enum_members "$native" "enum class $enum_name " \
            >"$test_directory/native-$enum_name"
        enum_members "$managed" "internal enum $enum_name" \
            >"$test_directory/managed-$enum_name"
        checks=$((checks + 1))
        if [[ ! -s "$test_directory/native-$enum_name" ]] ||
            ! cmp -s "$test_directory/native-$enum_name" \
                "$test_directory/managed-$enum_name"; then
            printf 'FAIL: managed %s mirrors the native enum order\n' \
                "$enum_name" >&2
            failures=$((failures + 1))
        fi
    done
}

if (($# > 1)); then
    printf '%s\n' 'FAIL: script tests accept at most one mode' >&2
    exit 1
fi
if [[ "${1-}" == "build-identity" ]]; then
    run_build_identity_contracts
    exit $?
fi
if (($# != 0)); then
    printf 'FAIL: unknown script-test mode: %s\n' "$1" >&2
    exit 1
fi

compiler="${CXX:-c++}"
if ! command -v "$compiler" >/dev/null 2>&1; then
    printf 'FAIL: C++ compiler is unavailable: %s\n' "$compiler" >&2
    exit 1
fi
printf '%s\n' \
    'extern "C" __attribute__((visibility("default"), used))' \
    'const char kueBuildIdentity[] = "KUE_BUILD_ID=script-tests";' |
    "$compiler" -std=c++20 -shared -fPIC \
        -Wall -Wextra -Wpedantic -Werror -x c++ - \
        -o "$test_directory/kuelethal.so"
cp -- "$project_root/config/kuelethal.json" "$test_directory/config.json"

run_entry_validation_contracts
run_process_search_contracts
run_bridge_enum_contracts

mkdir "$test_directory/missing-helper-project"
cp -a -- "$project_root/scripts" "$test_directory/missing-helper-project/"
find "$test_directory/missing-helper-project/scripts" -maxdepth 1 \
    -type f -name 'entry-validation.sh' -delete
run_command "$test_directory/missing-helper-project/scripts/build.sh" \
    >"$test_directory/stdout" 2>"$test_directory/stderr"
expect_failure "$command_status" "build fails visibly without entry validation"
expect_contains 'entry validation is unavailable' "$test_directory/stderr" \
    "build identifies the missing required validation owner"
for entry_script in build.sh inject.sh run.sh; do
    expect_count 1 'source "$entry_validation"' \
        "$project_root/scripts/$entry_script" \
        "$entry_script sources the canonical entry validation owner"
done

run_command env \
    KUE_MODULE="$test_directory/kuelethal.so" \
    KUE_CONFIG="$test_directory/config.json" \
    KUE_LOG="$test_directory/valid.log" \
    "$project_root/scripts/run.sh" /usr/bin/true \
    >"$test_directory/stdout" 2>"$test_directory/stderr"
expect_success "$command_status" "run executes a valid payload"

: >"$test_directory/empty.so"
run_command env KUE_MODULE="$test_directory/empty.so" \
    KUE_CONFIG="$test_directory/config.json" \
    KUE_LOG="$test_directory/empty.log" \
    "$project_root/scripts/run.sh" \
    /usr/bin/touch "$test_directory/empty-ran" >"$test_directory/stdout" \
    2>"$test_directory/stderr"
expect_failure "$command_status" "run rejects an empty module"
expect_absent "$test_directory/empty-ran" \
    "empty module cannot execute the payload"

mkdir "$test_directory/path with space"
cp -- "$test_directory/kuelethal.so" \
    "$test_directory/path with space/kuelethal.so"
run_command env KUE_MODULE="$test_directory/path with space/kuelethal.so" \
    KUE_CONFIG="$test_directory/config.json" \
    KUE_LOG="$test_directory/space.log" \
    "$project_root/scripts/run.sh" /usr/bin/touch "$test_directory/space-ran" \
    >"$test_directory/stdout" 2>"$test_directory/stderr"
expect_failure "$command_status" \
    "run rejects an unrepresentable LD_PRELOAD path"
expect_absent "$test_directory/space-ran" \
    "separator-bearing module cannot execute the payload"

mkdir "$test_directory/working"
ln -s ../kuelethal.so "$test_directory/working/module.so"
ln -s ../config.json "$test_directory/working/config.json"
: >"$test_directory/canonical.log"
ln -s ../canonical.log "$test_directory/working/log.link"
if (
    cd "$test_directory/working" || exit 1
    env KUE_MODULE=module.so KUE_CONFIG=config.json KUE_LOG=log.link \
        "$project_root/scripts/run.sh" /usr/bin/env
) >"$test_directory/environment" 2>"$test_directory/stderr"; then
    command_status=0
else
    command_status=$?
fi
expect_success "$command_status" "run accepts a valid canonicalizable launch"
expect_line "KUE_CONFIG=$test_directory/config.json" \
    "$test_directory/environment" \
    "run exports the canonical config path"
expect_line "KUE_LOG=$test_directory/canonical.log" \
    "$test_directory/environment" \
    "run exports the canonical log target"
expect_line "LD_PRELOAD=$test_directory/kuelethal.so" \
    "$test_directory/environment" \
    "run exports one canonical preload token"

for variable in KUE_MODULE KUE_CONFIG KUE_LOG; do
    run_command env KUE_MODULE="$test_directory/kuelethal.so" \
        KUE_CONFIG="$test_directory/config.json" \
        KUE_LOG="$test_directory/run.log" "$variable=" \
        "$project_root/scripts/run.sh" /usr/bin/touch \
        "$test_directory/$variable-ran" >"$test_directory/stdout" \
        2>"$test_directory/stderr"
    expect_failure "$command_status" "run rejects empty $variable"
    expect_absent "$test_directory/$variable-ran" \
        "empty $variable cannot execute the payload"
done

printf -v multibyte_path '%*s' 2049 ''
multibyte_path="${multibyte_path// /é}"
run_command env KUE_MODULE="$multibyte_path" \
    KUE_CONFIG="$test_directory/config.json" \
    KUE_LOG="$test_directory/multibyte.log" \
    "$project_root/scripts/run.sh" /usr/bin/true \
    >"$test_directory/stdout" 2>"$test_directory/stderr"
expect_failure "$command_status" \
    "run enforces the module path bound in UTF-8 bytes"
expect_line 'error: module path exceeds its text contract' \
    "$test_directory/stderr" \
    "run reports the byte-bounded module path contract"

invalid_utf8=$'\xc0\xaf'
run_command env KUE_MODULE="$invalid_utf8" \
    KUE_CONFIG="$test_directory/config.json" \
    KUE_LOG="$test_directory/invalid-utf8.log" \
    "$project_root/scripts/run.sh" /usr/bin/true \
    >"$test_directory/stdout" 2>"$test_directory/stderr"
expect_failure "$command_status" "run rejects ill-formed UTF-8 input"
expect_line 'error: module path must be valid UTF-8' "$test_directory/stderr" \
    "run identifies the UTF-8 boundary failure"

mkdir "$test_directory/locale-bin"
printf '%s\n' '#!/usr/bin/env bash' \
    'if [[ "${LC_ALL-}" != "C" ]]; then exit 65; fi' \
    'exec /usr/bin/file "$@"' >"$test_directory/locale-bin/file"
chmod +x "$test_directory/locale-bin/file"
run_command env LC_ALL=C.UTF-8 PATH="$test_directory/locale-bin:$PATH" \
    KUE_MODULE="$test_directory/kuelethal.so" \
    KUE_CONFIG="$test_directory/config.json" \
    KUE_LOG="$test_directory/locale.log" \
    "$project_root/scripts/run.sh" /usr/bin/true \
    >"$test_directory/stdout" 2>"$test_directory/stderr"
expect_success "$command_status" "run parses machine output under the C locale"

touch "$test_directory/kue-inject.exe" "$test_directory/kuelethal.dll"
mkdir "$test_directory/pe-bin"
printf '%s\n' '#!/usr/bin/env bash' \
    'case "${!#}" in' \
    '    *.exe) printf "%s\n" "PE32+ executable (console) x86-64, for MS Windows" ;;' \
    '    *.dll) printf "%s\n" "PE32+ executable (DLL) (console) x86-64, for MS Windows" ;;' \
    '    *) printf "%s\n" "ELF 64-bit LSB shared object, x86-64" ;;' \
    'esac' >"$test_directory/pe-bin/file"
printf '%s\n' '#!/usr/bin/env bash' \
    'printf "%s\n" invoked > "$SCRIPT_PGREP_MARKER"' \
    'exit 1' >"$test_directory/pe-bin/pgrep"
chmod +x "$test_directory/pe-bin/file" "$test_directory/pe-bin/pgrep"

run_command env KUE_INJECTOR="$test_directory/missing.exe" \
    KUE_MODULE="$test_directory/kuelethal.dll" \
    KUE_CONFIG="$test_directory/config.json" \
    KUE_LOG="$test_directory/uncreated/inject.log" \
    "$project_root/scripts/inject.sh" \
    >"$test_directory/stdout" 2>"$test_directory/stderr"
expect_failure "$command_status" "inject rejects an explicit missing injector"
expect_absent "$test_directory/uncreated" \
    "inject does not prepare state for a missing injector override"
expect_line "[kue] error: configured injector not found: $test_directory/missing.exe" \
    "$test_directory/stderr" "inject identifies the missing injector override"

run_command env KUE_INJECTOR="$test_directory/kue-inject.exe" \
    KUE_MODULE="$test_directory/missing.dll" \
    KUE_CONFIG="$test_directory/config.json" \
    KUE_LOG="$test_directory/uncreated/inject.log" \
    "$project_root/scripts/inject.sh" \
    >"$test_directory/stdout" 2>"$test_directory/stderr"
expect_failure "$command_status" "inject rejects an explicit missing module"
expect_line "[kue] error: configured module not found: $test_directory/missing.dll" \
    "$test_directory/stderr" "inject identifies the missing module override"

run_command env PATH="$test_directory/pe-bin:$PATH" \
    SCRIPT_PGREP_MARKER="$test_directory/linux-module-pgrep-ran" \
    KUE_INJECTOR="$test_directory/kue-inject.exe" \
    KUE_MODULE="$test_directory/kuelethal.so" \
    KUE_CONFIG="$test_directory/config.json" \
    KUE_LOG="$test_directory/linux-module.log" \
    "$project_root/scripts/inject.sh" \
    >"$test_directory/stdout" 2>"$test_directory/stderr"
expect_failure "$command_status" "inject rejects a Linux module"
expect_absent "$test_directory/linux-module-pgrep-ran" \
    "inject rejects the module format before process discovery"
expect_contains 'is not an x86-64 Windows DLL' "$test_directory/stderr" \
    "inject identifies the Windows module contract"

run_command env PATH="$test_directory/pe-bin:$PATH" \
    SCRIPT_PGREP_MARKER="$test_directory/no-target-pgrep-ran" \
    KUE_INJECTOR="$test_directory/kue-inject.exe" \
    KUE_MODULE="$test_directory/kuelethal.dll" \
    KUE_CONFIG="$test_directory/config.json" \
    KUE_LOG="$test_directory/inject.log" \
    "$project_root/scripts/inject.sh" \
    >"$test_directory/stdout" 2>"$test_directory/stderr"
expect_failure "$command_status" "inject reports a bounded no-target search"
expect_line "[kue] error: no running game matched 'Lethal Company.exe'" \
    "$test_directory/stdout" "inject reports the exact no-target contract"

eval "$(sed -n '/^windows_path() {/,/^}/p' "$project_root/scripts/inject.sh")"
checks=$((checks + 1))
if [[ "$(windows_path '/home/player/Kue Lethal/build-windows/kuelethal.dll')" != \
    'Z:\home\player\Kue Lethal\build-windows\kuelethal.dll' ]]; then
    printf '%s\n' 'FAIL: inject maps host paths onto drive Z:' >&2
    failures=$((failures + 1))
fi
expect_count 0 'gdb' "$project_root/scripts/inject.sh" \
    "inject loads the module without a debugger"

mkdir "$test_directory/target-bin"
printf '%s\n' '#!/usr/bin/env bash' \
    'printf "%s\n" "/games/Lethal Company/Lethal Company.exe"' \
    >"$test_directory/target-bin/tr"
printf '%s\n' '#!/usr/bin/env bash' 'printf "%s\n" "/usr/bin/true"' \
    >"$test_directory/target-bin/readlink"
printf '%s\n' '#!/usr/bin/env bash' \
    'printf "%s\n" "ELF 64-bit LSB pie executable, x86-64"' \
    >"$test_directory/target-bin/file"
printf '%s\n' '#!/usr/bin/env bash' 'exit 0' >"$test_directory/target-bin/grep"
chmod +x \
    "$test_directory/target-bin/tr" "$test_directory/target-bin/readlink" \
    "$test_directory/target-bin/file" "$test_directory/target-bin/grep"
sleep 30 &
selection_worker=$!
if {
    sed -n '/^pick_target() {/,/^}/p' "$project_root/scripts/inject.sh"
    printf 'candidate_pids=("%s" "%s")\n' "$$" "$selection_worker"
    printf '%s\n' 'required_architecture=64' 'pick_target'
} | PATH="$test_directory/target-bin:$PATH" bash \
    >"$test_directory/target-output" 2>"$test_directory/target-error"; then
    command_status=0
else
    command_status=$?
fi
if ! kill "$selection_worker"; then
    printf '%s\n' 'FAIL: cannot stop target-selection fixture' >&2
    failures=$((failures + 1))
fi
if wait "$selection_worker"; then
    printf '%s\n' \
        'FAIL: target-selection fixture exited before termination' >&2
    failures=$((failures + 1))
else
    selection_wait_status=$?
    if ((selection_wait_status != 143)); then
        printf 'FAIL: target fixture status is %d instead of 143\n' \
            "$selection_wait_status" >&2
        failures=$((failures + 1))
    fi
fi
selection_worker=""
expect_failure "$command_status" \
    "inject rejects multiple valid matching game processes"
expect_contains 'multiple matching game processes' \
    "$test_directory/target-error" \
    "inject reports ambiguous target candidates"

printf '%s\n' \
    '#!/usr/bin/env bash' \
    'map_path="${@: -1}"' \
    '[[ "$map_path" == "/proc/$KUE_MAPPED_PID/maps" ]]' \
    >"$test_directory/target-bin/grep"
chmod +x "$test_directory/target-bin/grep"
sleep 30 &
selection_worker=$!
mapped_target_pid="$selection_worker"
if {
    sed -n '/^pick_target() {/,/^}/p' "$project_root/scripts/inject.sh"
    printf 'candidate_pids=("%s" "%s")\n' "$$" "$selection_worker"
    printf '%s\n' 'required_architecture=64' 'pick_target'
} | KUE_MAPPED_PID="$selection_worker" \
    PATH="$test_directory/target-bin:$PATH" bash \
    >"$test_directory/target-output" 2>"$test_directory/target-error"; then
    command_status=0
else
    command_status=$?
fi
if ! kill "$selection_worker"; then
    printf '%s\n' 'FAIL: cannot stop mapped-target fixture' >&2
    failures=$((failures + 1))
fi
if wait "$selection_worker"; then
    printf '%s\n' 'FAIL: mapped-target fixture exited early' >&2
    failures=$((failures + 1))
else
    selection_wait_status=$?
    if ((selection_wait_status != 143)); then
        printf 'FAIL: mapped target status is %d instead of 143\n' \
            "$selection_wait_status" >&2
        failures=$((failures + 1))
    fi
fi
selection_worker=""
expect_success "$command_status" \
    "inject prefers the process with a mapped UnityPlayer image"
expect_contains "$mapped_target_pid" "$test_directory/target-output" \
    "inject selects the mapped Unity process over command wrappers"

mkdir "$test_directory/bin"
printf '%s\n' \
    '#!/usr/bin/env bash' \
    'printf "%s\n" "$*" >> "$SCRIPT_CMAKE_LOG"' \
    'if [[ "$1" == "-S" ]]; then' \
    '  build_directory=""' \
    '  module_enabled=0' \
    '  while (( $# != 0 )); do' \
    '    if [[ "$1" == "-B" && $# -ge 2 ]]; then' \
    '      build_directory="$2"' \
    '      shift' \
    '    fi' \
    '    if [[ "$1" == "-DKUE_BUILD_MODULE=ON" ]]; then module_enabled=1; fi' \
    '    shift' \
    '  done' \
    '  if [[ -n "$build_directory" && "$module_enabled" == 1 ]]; then' \
    '    printf "%s\n" ON > "$build_directory/module-enabled"' \
    '  fi' \
    '  exit 0' \
    'fi' \
    'if [[ "$1" == "--build" ]]; then' \
    '  build_directory="$2"' \
    '  requested_module=0' \
    '  for argument in "$@"; do' \
    '    if [[ "$argument" == "kuelethal" ]]; then requested_module=1; fi' \
    '  done' \
    '  if [[ -f "$build_directory/module-enabled" ]] &&' \
    '      [[ "$requested_module" == 1 ]]; then' \
    '    cp -- "$SCRIPT_TEST_MODULE" "$build_directory/kuelethal.so"' \
    '  fi' \
    '  exit 0' \
    'fi' \
    'exit 64' >"$test_directory/bin/cmake"
chmod +x "$test_directory/bin/cmake"
mkdir "$test_directory/build output"
printf '%s\n' OFF >"$test_directory/build output/module-enabled"
printf '%s\n' stale >"$test_directory/build output/kuelethal.so"
run_command env PATH="$test_directory/bin:$PATH" \
    SCRIPT_TEST_MODULE="$test_directory/kuelethal.so" \
    SCRIPT_CMAKE_LOG="$test_directory/cmake.log" \
    BUILD_DIR="$test_directory/build output" JOBS=2 \
    "$project_root/scripts/build.sh" \
    >"$test_directory/stdout" 2>"$test_directory/stderr"
expect_success "$command_status" \
    "build accepts canonical paths and a bounded job count"
checks=$((checks + 1))
if [[ ! -s "$test_directory/build output/kuelethal.so" ]]; then
    printf '%s\n' 'FAIL: build validates a nonempty module result' >&2
    failures=$((failures + 1))
fi
checks=$((checks + 1))
if ! cmp -s -- "$test_directory/kuelethal.so" \
    "$test_directory/build output/kuelethal.so"; then
    printf '%s\n' \
        'FAIL: build replaces a stale artifact through its target' >&2
    failures=$((failures + 1))
fi
expect_contains '-DKUE_BUILD_MODULE=ON' "$test_directory/cmake.log" \
    "build explicitly enables the production module in a reused cache"
expect_contains '--target kuelethal' "$test_directory/cmake.log" \
    "build requests the production module target directly"

run_command env PATH="$test_directory/bin:$PATH" \
    SCRIPT_TEST_MODULE="$test_directory/kuelethal.so" \
    SCRIPT_CMAKE_LOG="$test_directory/cmake-windows.log" \
    BUILD_DIR="$test_directory/build windows" TARGET=windows \
    "$project_root/scripts/build.sh" \
    >"$test_directory/stdout" 2>"$test_directory/stderr"
expect_failure "$command_status" \
    "build for Windows fails visibly without the Windows artifacts"
expect_contains '-DCMAKE_TOOLCHAIN_FILE=' "$test_directory/cmake-windows.log" \
    "build for Windows configures the MinGW toolchain"
expect_contains '--target kuelethal kue-inject' \
    "$test_directory/cmake-windows.log" \
    "build for Windows requests the module and injector targets"
expect_contains 'build did not produce a nonempty artifact' \
    "$test_directory/stderr" \
    "build for Windows identifies the missing artifact"

run_command env TARGET=macos "$project_root/scripts/build.sh" \
    >"$test_directory/stdout" 2>"$test_directory/stderr"
expect_failure "$command_status" "build rejects an unsupported TARGET"

for jobs in '' 0 invalid 1025; do
    run_command env PATH="$test_directory/bin:$PATH" \
        SCRIPT_TEST_MODULE="$test_directory/kuelethal.so" \
        SCRIPT_CMAKE_LOG="$test_directory/cmake.log" \
        BUILD_DIR="$test_directory/build output" JOBS="$jobs" \
        "$project_root/scripts/build.sh" \
        >"$test_directory/stdout" 2>"$test_directory/stderr"
    expect_failure "$command_status" "build rejects invalid JOBS=$jobs"
done

run_command env BUILD_DIR= "$project_root/scripts/build.sh" \
    >"$test_directory/stdout" 2>"$test_directory/stderr"
expect_failure "$command_status" "build rejects an explicitly empty BUILD_DIR"

if ((failures == 0)); then
    printf 'script tests passed: %d checks\n' "$checks"
    exit 0
fi
printf 'script tests failed: %d of %d checks\n' "$failures" "$checks" >&2
exit 1
