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

for command_name in cmake iconv mkdir realpath; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        printf 'error: required command is unavailable: %s\n' \
            "$command_name" >&2
        exit 1
    fi
done

validate_text "$project_root" 4096 "project root"

if [[ -v TARGET && "$TARGET" != "linux" && "$TARGET" != "windows" ]]; then
    printf 'error: TARGET must be linux or windows, got: %s\n' "$TARGET" >&2
    exit 1
fi
target="${TARGET-linux}"
if [[ -v BUILD_DIR && -z "$BUILD_DIR" ]]; then
    printf '%s\n' 'error: BUILD_DIR is set but empty' >&2
    exit 1
fi
if [[ "$target" == "windows" ]]; then
    build_directory="${BUILD_DIR-$project_root/build-windows}"
else
    build_directory="${BUILD_DIR-$project_root/build}"
fi
validate_text "$build_directory" 4096 "build directory"
if ! mkdir -p -- "$build_directory"; then
    printf 'error: cannot create build directory: %s\n' "$build_directory" >&2
    exit 1
fi
if ! canonicalize_existing "$build_directory" build_directory; then
    printf 'error: cannot resolve build directory: %s\n' "$build_directory" >&2
    exit 1
fi
validate_text "$build_directory" 4096 "resolved build directory"

if [[ -v JOBS ]]; then
    invalid_jobs=0
    if [[ ! "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
        invalid_jobs=1
    elif ((${#JOBS} > 4)); then
        invalid_jobs=1
    elif ((10#$JOBS > 1024)); then
        invalid_jobs=1
    fi
    if ((invalid_jobs != 0)); then
        printf '%s%s\n' \
            'error: JOBS must be a decimal integer from 1 to 1024, got: ' \
            "$JOBS" >&2
        exit 1
    fi
fi

configure_arguments=(-DCMAKE_BUILD_TYPE=Release -DKUE_BUILD_MODULE=ON)
build_targets=(kuelethal)
artifacts=("$build_directory/kuelethal.so")
if [[ "$target" == "windows" ]]; then
    configure_arguments+=(
        "-DCMAKE_TOOLCHAIN_FILE=$project_root/cmake/MinGWx86_64.cmake"
    )
    build_targets=(kuelethal kue-inject)
    artifacts=("$build_directory/kuelethal.dll" "$build_directory/kue-inject.exe")
fi
cmake -S "$project_root" -B "$build_directory" "${configure_arguments[@]}"
if [[ -v JOBS ]]; then
    cmake --build "$build_directory" --target "${build_targets[@]}" --parallel "$JOBS"
else
    cmake --build "$build_directory" --target "${build_targets[@]}" --parallel
fi

for artifact in "${artifacts[@]}"; do
    if [[ ! -f "$artifact" || ! -s "$artifact" ]]; then
        printf 'error: build did not produce a nonempty artifact: %s\n' "$artifact" >&2
        exit 1
    fi
done
printf '[kue] release build complete: %s\n' "${artifacts[@]}"
