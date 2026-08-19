validate_text() {
    local value="$1"
    local maximum_bytes="$2"
    local purpose="$3"
    local LC_ALL=C
    if ((${#value} > maximum_bytes)) || [[ "$value" == *[[:cntrl:]]* ]]; then
        printf '%serror: %s exceeds its text contract\n' \
            "${entry_error_prefix-}" "$purpose" >&2
        return 1
    fi
    if [[ "$value" != *[!$'\x20'-$'\x7e']* ]]; then
        return 0
    fi
    if ! printf '%s' "$value" | iconv -f UTF-8 -t UTF-8 >/dev/null 2>&1; then
        printf '%serror: %s must be valid UTF-8\n' \
            "${entry_error_prefix-}" "$purpose" >&2
        return 1
    fi
}

canonicalize_existing() {
    local input_path="$1"
    local destination_name="$2"
    local -a canonical_records=()
    local command_status_text command_status
    mapfile -d '' -t -n 2 canonical_records < <(
        local realpath_status=0
        realpath -e -z -- "$input_path" 2>/dev/null || realpath_status=$?
        printf 'KUE_REALPATH_STATUS=%d\0' "$realpath_status"
    )
    local status_pattern='^KUE_REALPATH_STATUS=([0-9]{1,3})$'
    if ((${#canonical_records[@]} == 1)) &&
        [[ "${canonical_records[0]}" =~ $status_pattern ]]; then
        return 1
    fi
    if ((${#canonical_records[@]} != 2)) ||
        [[ ! "${canonical_records[1]}" =~ $status_pattern ]]; then
        return 1
    fi
    command_status_text="${BASH_REMATCH[1]}"
    command_status=$((10#$command_status_text))
    if ((command_status != 0)); then
        return 1
    fi
    printf -v "$destination_name" '%s' "${canonical_records[0]}"
}
