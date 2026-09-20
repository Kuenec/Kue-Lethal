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

fixture_symbol_address() {
    local target_pid="$1"
    local symbol="$2"
    local libc_line libc_base libc_path target_libc symbol_offset
    libc_line="$(
        awk '$2 ~ /r--p/ && $3 == "00000000" && $NF ~ /\/libc\.so\.6$/ {
            print
            exit
        }' "/proc/$target_pid/maps"
    )"
    libc_base="${libc_line%%-*}"
    libc_path="${libc_line##* }"
    target_libc="/proc/$target_pid/root$libc_path"
    if [[ -z "$libc_line" || ! -r "$target_libc" ]]; then
        return 1
    fi
    symbol_offset="$(
        LC_ALL=C readelf -Ws "$target_libc" |
            awk -v requested="$symbol" '
                $4 == "FUNC" && $8 ~ ("^" requested "@@") && !value {
                    value=$2
                }
                END { print value }
            '
    )"
    if [[ -z "$symbol_offset" ]]; then
        return 1
    fi
    printf '0x%x' "$((16#$libc_base + 16#$symbol_offset))"
}

run_environment_rollback_case() {
    local mode="$1"
    local fixture="$2"
    local failure_module="$3"
    local real_gdb="$4"
    local ready_fifo="$test_directory/$mode-ready"
    local release_fifo="$test_directory/$mode-release"
    local gdb_commands="$test_directory/$mode-gdb-commands"
    local gdb_output="$test_directory/$mode-gdb-output"
    mkfifo "$ready_fifo" "$release_fifo"
    "$fixture" "$mode" "$ready_fifo" "$release_fifo" "$failure_module" &
    selection_worker=$!
    if ! IFS= read -r ready <"$ready_fifo" || [[ "$ready" != "ready" ]]; then
        printf 'FAIL: %s rollback fixture does not become ready\n' "$mode" >&2
        failures=$((failures + 1))
        return
    fi

    REAL_DLOPEN="$(fixture_symbol_address "$selection_worker" dlopen)"
    REAL_DLCLOSE="$(fixture_symbol_address "$selection_worker" dlclose)"
    REAL_DLERROR="$(fixture_symbol_address "$selection_worker" dlerror)"
    REAL_DLSYM="$(fixture_symbol_address "$selection_worker" dlsym)"
    REAL_ERRNO_LOCATION="$(
        fixture_symbol_address "$selection_worker" __errno_location
    )"
    REAL_FREE="$(fixture_symbol_address "$selection_worker" free)"
    REAL_GETENV="$(fixture_symbol_address "$selection_worker" getenv)"
    REAL_SETENV="$(fixture_symbol_address "$selection_worker" setenv)"
    REAL_STRDUP="$(fixture_symbol_address "$selection_worker" strdup)"
    REAL_UNSETENV="$(fixture_symbol_address "$selection_worker" unsetenv)"
    module_gdb="$failure_module"
    config_gdb="replacement-config"
    log_gdb="replacement-log"
    if ! write_gdb_commands "$gdb_commands" ||
        ! "$real_gdb" -q -p "$selection_worker" -batch \
            -x "$gdb_commands" </dev/null \
            >"$gdb_output" 2>&1; then
        printf 'FAIL: %s rollback does not complete in one attach\n' \
            "$mode" >&2
        sed 's/^/  /' "$gdb_output" >&2
        failures=$((failures + 1))
    fi
    if ! grep -Fq 'kue_start failed: result=3' "$gdb_output" ||
        ! grep -Fq 'failed startup target environment restored' \
            "$gdb_output" ||
        ! grep -Fq 'failed startup module handle released' "$gdb_output"; then
        sed 's/^/  /' "$gdb_output" >&2
    fi
    checks=$((checks + 1))
    if ! grep -Fq 'kue_start failed: result=3' "$gdb_output"; then
        printf 'FAIL: %s rollback reaches the failed start\n' "$mode" >&2
        failures=$((failures + 1))
    fi
    checks=$((checks + 1))
    if ! grep -Fq 'failed startup target environment restored' \
        "$gdb_output"; then
        printf 'FAIL: %s rollback reports restored target state\n' \
            "$mode" >&2
        failures=$((failures + 1))
    fi
    checks=$((checks + 1))
    if ! grep -Fq 'failed startup module handle released' "$gdb_output"; then
        printf 'FAIL: %s rollback reports released failed module\n' \
            "$mode" >&2
        failures=$((failures + 1))
    fi

    printf '%s\n' release >"$release_fifo"
    if wait "$selection_worker"; then
        checks=$((checks + 1))
    else
        fixture_status=$?
        checks=$((checks + 1))
        printf 'FAIL: %s rollback postcondition failed with status %d\n' \
            "$mode" "$fixture_status" >&2
        failures=$((failures + 1))
    fi
    selection_worker=""
}

run_environment_rollback_contracts() {
    local compiler_path real_gdb
    compiler_path="$(command -v "${CXX:-c++}")"
    real_gdb="$(command -v gdb)"
    printf '%s\n' \
        '#include <cerrno>' \
        '#include <cstdlib>' \
        '#include <cstring>' \
        '#include <dlfcn.h>' \
        '#include <fcntl.h>' \
        '#include <sys/prctl.h>' \
        '#include <unistd.h>' \
        'bool writeAll(int descriptor, const char* text, std::size_t size) {' \
        '  while (size != 0) {' \
        '    const ssize_t written = write(descriptor, text, size);' \
        '    if (written > 0) {' \
        '      text += written;' \
        '      size -= static_cast<std::size_t>(written);' \
        '      continue;' \
        '    }' \
        '    if (written < 0 && errno == EINTR) continue;' \
        '    return false;' \
        '  }' \
        '  return true;' \
        '}' \
        'int openRead(const char* path) {' \
        '  for (;;) {' \
        '    const int descriptor = open(path, O_RDONLY | O_CLOEXEC);' \
        '    if (descriptor >= 0 || errno != EINTR) return descriptor;' \
        '  }' \
        '}' \
        'int main(int argc, char** argv) {' \
        '  if (argc != 5 ||' \
        '      prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY) != 0) {' \
        '    return 10;' \
        '  }' \
        '  const bool initiallySet = std::strcmp(argv[1], "set") == 0;' \
        '  if (initiallySet) {' \
        '    if (setenv("KUE_CONFIG", "original-config", 1) != 0 ||' \
        '        setenv("KUE_LOG", "original-log", 1) != 0) return 11;' \
        '  } else if (std::strcmp(argv[1], "unset") != 0 ||' \
        '             unsetenv("KUE_CONFIG") != 0 ||' \
        '             unsetenv("KUE_LOG") != 0) {' \
        '    return 12;' \
        '  }' \
        '  const int ready = open(argv[2], O_WRONLY | O_CLOEXEC);' \
        '  if (ready < 0 || !writeAll(ready, "ready\n", 6) ||' \
        '      close(ready) != 0) {' \
        '    return 13;' \
        '  }' \
        '  const int release = openRead(argv[3]);' \
        '  char signal = 0;' \
        '  if (release < 0 || read(release, &signal, 1) != 1 ||' \
        '      close(release) != 0) {' \
        '    return 14;' \
        '  }' \
        '  const char* config = getenv("KUE_CONFIG");' \
        '  const char* log = getenv("KUE_LOG");' \
        '  if (initiallySet) {' \
        '    if (!config || !log ||' \
        '        std::strcmp(config, "original-config") != 0 ||' \
        '        std::strcmp(log, "original-log") != 0) return 15;' \
        '  } else if (config || log) {' \
        '    return 16;' \
        '  }' \
        '  void* loaded = dlopen(argv[4], RTLD_NOW | RTLD_NOLOAD);' \
        '  if (loaded) { dlclose(loaded); return 17; }' \
        '  return 0;' \
        '}' >"$test_directory/environment-fixture.cpp"
    printf '%s\n' \
        'extern "C" __attribute__((visibility("default")))' \
        'int kue_start() noexcept {' \
        '  return 3;' \
        '}' \
        >"$test_directory/failure-module.cpp"
    "$compiler_path" -std=c++20 -Wall -Wextra -Wpedantic -Werror \
        "$test_directory/environment-fixture.cpp" -ldl \
        -o "$test_directory/environment-fixture"
    "$compiler_path" -std=c++20 -shared -fPIC \
        -Wall -Wextra -Wpedantic -Werror \
        "$test_directory/failure-module.cpp" \
        -o "$test_directory/failure-module.so"
    eval "$(
        sed -n '/^write_gdb_commands() {/,/^}/p' \
            "$project_root/scripts/inject.sh"
    )"
    run_environment_rollback_case unset "$test_directory/environment-fixture" \
        "$test_directory/failure-module.so" "$real_gdb"
    run_environment_rollback_case set "$test_directory/environment-fixture" \
        "$test_directory/failure-module.so" "$real_gdb"
    if ((failures == 0)); then
        printf 'inject environment tests passed: %d checks\n' "$checks"
        return 0
    fi
    printf 'inject environment tests failed: %d of %d checks\n' \
        "$failures" "$checks" >&2
    return 1
}

run_runtime_result_case() {
    local log_path="$1"
    local output_path="$2"
    if {
        sed -n '/^fresh_log() {/,/^}/p; /^report_runtime_result() {/,/^}/p' \
            "$project_root/scripts/inject.sh"
        printf '%s\n' 'sleep() { :; }' 'report_runtime_result'
    } | env KUE_LOG="$log_path" BUILD_ID='KUE_BUILD_ID=script-tests' \
        log_start_bytes=0 \
        log_start_identity='' maximum_fresh_log_bytes=$((1024 * 1024)) bash \
        >"$output_path" 2>&1; then
        command_status=0
    else
        command_status=$?
    fi
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
    local collector_source classifier_source status
    local permission_failure transaction_permission_failure
    collector_source="$(
        sed -n '/^collect_candidate_pids() {/,/^}/p' \
            "$project_root/scripts/inject.sh"
    )"
    classifier_source="$(
        sed -n '/^is_pre_mutation_permission_failure() {/,/^}/p' \
            "$project_root/scripts/inject.sh"
    )"

    checks=$((checks + 1))
    if [[ -z "$collector_source" ]]; then
        printf '%s\n' \
            'FAIL: inject exposes one bounded candidate collector' >&2
        failures=$((failures + 1))
        return
    fi
    checks=$((checks + 1))
    if [[ -z "$classifier_source" ]]; then
        printf '%s\n' \
            'FAIL: inject exposes one permission classifier' >&2
        failures=$((failures + 1))
        return
    fi
    eval "$collector_source"
    eval "$classifier_source"
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

    permission_failure='ptrace: Operation not permitted.'
    transaction_permission_failure='kue target transaction entered'
    transaction_permission_failure+=$'\nptrace: Operation not permitted.'
    if is_pre_mutation_permission_failure 1 "$permission_failure"; then
        status=0
    else
        status=$?
    fi
    expect_success "$status" \
        "inject recognizes one pre-mutation permission failure"

    if is_pre_mutation_permission_failure 0 "$permission_failure"; then
        status=0
    else
        status=$?
    fi
    expect_failure "$status" "inject never retries a successful attach status"

    if is_pre_mutation_permission_failure \
        1 "$transaction_permission_failure"; then
        status=0
    else
        status=$?
    fi
    expect_failure "$status" \
        "inject never retries after the target transaction starts"

    if is_pre_mutation_permission_failure 1 'unclassified attach failure'; then
        status=0
    else
        status=$?
    fi
    expect_failure "$status" \
        "inject never retries an unclassified attach failure"
}

if (($# > 1)); then
    printf '%s\n' 'FAIL: script tests accept at most one mode' >&2
    exit 1
fi
if [[ "${1-}" == "build-identity" ]]; then
    run_build_identity_contracts
    exit $?
fi
if [[ "${1-}" == "inject-environment" ]]; then
    run_environment_rollback_contracts
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

mkdir "$test_directory/aarch64-bin"
printf '%s\n' '#!/usr/bin/env bash' \
    'printf "%s\n" "ELF 64-bit LSB shared object, ARM aarch64"' \
    >"$test_directory/aarch64-bin/file"
chmod +x "$test_directory/aarch64-bin/file"
run_command env KUE_MODULE="$test_directory/missing.so" \
    KUE_CONFIG="$test_directory/missing.json" \
    KUE_LOG="$test_directory/uncreated/inject.log" \
    "$project_root/scripts/inject.sh" \
    >"$test_directory/stdout" 2>"$test_directory/stderr"
expect_failure "$command_status" "inject rejects an explicit missing module"
expect_absent "$test_directory/uncreated" \
    "inject does not prepare state for a missing override"
missing_module_message='[kue] error: configured module not found: '
missing_module_message+="$test_directory/missing.so"
expect_line "$missing_module_message" \
    "$test_directory/stderr" "inject identifies the missing override"

printf '%s\n' '#!/usr/bin/env bash' \
    'printf "%s\n" invoked > "$SCRIPT_PGREP_MARKER"' \
    'exit 1' >"$test_directory/aarch64-bin/pgrep"
chmod +x "$test_directory/aarch64-bin/pgrep"
run_command env PATH="$test_directory/aarch64-bin:$PATH" \
    SCRIPT_PGREP_MARKER="$test_directory/aarch64-pgrep-ran" \
    KUE_MODULE="$test_directory/kuelethal.so" \
    KUE_CONFIG="$test_directory/config.json" \
    KUE_LOG="$test_directory/aarch64-inject.log" \
    "$project_root/scripts/inject.sh" \
    >"$test_directory/stdout" 2>"$test_directory/stderr"
expect_failure "$command_status" \
    "inject rejects an ELF64 module for another architecture"
expect_absent "$test_directory/aarch64-pgrep-ran" \
    "inject rejects module architecture before process discovery"
expect_contains 'module is not an x86-64 ELF shared object' \
    "$test_directory/stderr" \
    "inject identifies the exact module architecture contract"

mkdir "$test_directory/bin"
printf '%s\n' '#!/usr/bin/env bash' 'exit 1' >"$test_directory/bin/pgrep"
chmod +x "$test_directory/bin/pgrep"

run_command env PATH="$test_directory/bin:$PATH" \
    KUE_MODULE="$test_directory/kuelethal.so" \
    KUE_CONFIG="$test_directory/config.json" \
    KUE_LOG="$test_directory/inject.log" \
    "$project_root/scripts/inject.sh" \
    >"$test_directory/stdout" 2>"$test_directory/stderr"
expect_failure "$command_status" "inject reports a bounded no-target search"
expect_line "[kue] error: no running game matched 'Lethal Company.exe'" \
    "$test_directory/stdout" "inject reports the exact no-target contract"

inject_source="$project_root/scripts/inject.sh"
generated_gdb_commands="$test_directory/generated-gdb-commands"
eval "$(sed -n '/^write_gdb_commands() {/,/^}/p' "$inject_source")"
REAL_DLOPEN=0x1
REAL_DLCLOSE=0x2
REAL_DLERROR=0x3
REAL_DLSYM=0x4
REAL_ERRNO_LOCATION=0x5
REAL_FREE=0x6
REAL_GETENV=0x7
REAL_SETENV=0x8
REAL_STRDUP=0x9
REAL_UNSETENV=0xa
module_gdb='/fixture/module.so'
config_gdb='/fixture/config.json'
log_gdb='/fixture/kuelethal.log'
write_gdb_commands "$generated_gdb_commands"

expect_contains 'set $real_dlsym = (void*(*)(void*, const char*))' \
    "$generated_gdb_commands" \
    "inject resolves symbols through target dlsym"
expect_contains 'set $real_dlclose = (int(*)(void*))' \
    "$generated_gdb_commands" \
    "inject resolves target dlclose for failed startup cleanup"
expect_contains 'set $start = $real_dlsym($h, "kue_start")' \
    "$generated_gdb_commands" \
    "inject resolves the exact native start entry"
expect_contains 'set $start_result = ((int(*)(void))$start)()' \
    "$generated_gdb_commands" \
    "inject invokes the start entry exactly once"
expect_contains 'if $start_result == 0' "$generated_gdb_commands" \
    "inject accepts only the successful start result"
expect_contains 'if $retain_handle == 0' "$generated_gdb_commands" \
    "inject releases only failed startup handles"
expect_contains 'set $close_result = $real_dlclose($h)' \
    "$generated_gdb_commands" \
    "inject makes failed startup retryable"
expect_contains 'set $real_getenv = (char*(*)(const char*))' \
    "$generated_gdb_commands" \
    "inject resolves target getenv for transactional environment capture"
expect_contains 'set $real_errno_location = (int*(*)(void))' \
    "$generated_gdb_commands" \
    "inject preserves target environment failure causes"
expect_contains 'set $real_unsetenv = (int(*)(const char*))' \
    "$generated_gdb_commands" \
    "inject resolves target unsetenv for transactional environment rollback"
expect_contains 'set $previous_config_value = $real_getenv("KUE_CONFIG")' \
    "$generated_gdb_commands" \
    "inject captures the target configuration environment"
expect_contains 'set $previous_log_value = $real_getenv("KUE_LOG")' \
    "$generated_gdb_commands" \
    "inject captures the target log environment"
expect_count 1 'set $config_restore_result = $real_unsetenv("KUE_CONFIG")' \
    "$generated_gdb_commands" \
    "inject has one unset-configuration rollback path"
expect_count 1 'set $log_restore_result = $real_unsetenv("KUE_LOG")' \
    "$generated_gdb_commands" \
    "inject has one unset-log rollback path"
config_restore_command='set $config_restore_result = '
config_restore_command+='$real_setenv("KUE_CONFIG", $previous_config, 1)'
expect_count 1 "$config_restore_command" "$generated_gdb_commands" \
    "inject has one set-configuration rollback path"
log_restore_command='set $log_restore_result = '
log_restore_command+='$real_setenv("KUE_LOG", $previous_log, 1)'
expect_count 1 "$log_restore_command" "$generated_gdb_commands" \
    "inject has one set-log rollback path"
rollback_failure='target environment rollback failed: KUE_CONFIG errno=%d, '
rollback_failure+='KUE_LOG errno=%d'
expect_contains "$rollback_failure" "$generated_gdb_commands" \
    "inject reports exact rollback failures"
expect_contains 'if $environment_changed != 0' "$generated_gdb_commands" \
    "inject restores target state only after a successful environment mutation"
expect_contains 'gdb_command_file="$(mktemp -t kue-inject-gdb.XXXXXX)"' \
    "$inject_source" "inject creates one private GDB command file"
expect_contains '-x "$gdb_command_file"' "$inject_source" \
    "inject executes conditional commands as one GDB source file"
checks=$((checks + 1))
if grep -Eq -- "-ex ' *(if|else|end)" "$project_root/scripts/inject.sh"; then
    printf '%s\n' \
        'FAIL: inject splits a GDB conditional across -ex arguments' >&2
    failures=$((failures + 1))
fi
expect_contains "if grep -q 'kue_start ok'" "$project_root/scripts/inject.sh" \
    "inject does not equate dlopen with startup success"
expect_contains 'if ((gdb_status != 0)); then' \
    "$project_root/scripts/inject.sh" \
    "inject reports local cleanup failure after startup without retrying"
expect_contains 'if ((root_gdb_status != 0)); then' \
    "$project_root/scripts/inject.sh" \
    "inject reports root cleanup failure after startup without hiding it"
permission_call='if ! is_pre_mutation_permission_failure '
permission_call+='"$gdb_status" "$out"; then'
expect_contains "$permission_call" \
    "$project_root/scripts/inject.sh" \
    "inject delegates root retry to the proven pre-mutation classifier"
checks=$((checks + 1))
start_invocations="$(grep -Fc 'set $start_result = ((int(*)(void))$start)()' \
    "$project_root/scripts/inject.sh")"
if [[ "$start_invocations" != "1" ]]; then
    printf '%s\n' \
        'FAIL: inject contains exactly one native start invocation' >&2
    failures=$((failures + 1))
fi
expect_count 1 'set $environment_changed = 1' "$generated_gdb_commands" \
    "inject performs one transactional environment mutation"
expect_count 1 'set $config_restore_result = 0' "$generated_gdb_commands" \
    "inject performs one environment rollback transaction"

printf '%s\n' 'KUE_BUILD_ID=script-tests' 'internal Unity HUD installed' \
    >"$test_directory/runtime-installed.log"
run_runtime_result_case "$test_directory/runtime-installed.log" \
    "$test_directory/runtime-installed-output"
expect_success "$command_status" "inject accepts an exact installed HUD marker"

printf '%s\n' 'KUE_BUILD_ID=script-tests' 'late HUD installer attempt 5/5' \
    >"$test_directory/runtime-exhausted.log"
run_runtime_result_case "$test_directory/runtime-exhausted.log" \
    "$test_directory/runtime-exhausted-output"
expect_failure "$command_status" \
    "inject rejects exhausted HUD delivery retries"

printf '%s\n' 'KUE_BUILD_ID=script-tests' 'main-thread HUD installer armed' \
    >"$test_directory/runtime-timeout.log"
run_runtime_result_case "$test_directory/runtime-timeout.log" \
    "$test_directory/runtime-timeout-output"
expect_failure "$command_status" \
    "inject rejects an unconfirmed HUD delivery timeout"
expect_contains 'HUD delivery was not confirmed' \
    "$test_directory/runtime-timeout-output" \
    "inject identifies the unconfirmed HUD delivery"
expect_contains 'fully exit the game before retrying' \
    "$test_directory/runtime-timeout-output" \
    "inject gives the only safe retry instruction after timeout"

mkdir "$test_directory/elf32-bin"
printf '%s\n' '#!/usr/bin/env bash' \
    'printf "%s\n" "ELF 32-bit LSB shared object, Intel 80386"' \
    >"$test_directory/elf32-bin/file"
chmod +x "$test_directory/elf32-bin/file"
run_command env PATH="$test_directory/elf32-bin:$test_directory/bin:$PATH" \
    KUE_MODULE="$test_directory/kuelethal.so" \
    KUE_CONFIG="$test_directory/config.json" \
    KUE_LOG="$test_directory/elf32.log" "$project_root/scripts/inject.sh" \
    >"$test_directory/stdout" 2>"$test_directory/stderr"
expect_failure "$command_status" \
    "inject rejects the unsupported 32-bit module path"
expect_contains 'module is not an x86-64 ELF shared object' \
    "$test_directory/stderr" \
    "inject identifies the declared architecture contract"

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
