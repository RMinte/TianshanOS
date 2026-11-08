# Shared host-test workspace. Explicit caller directories are never removed.
# Source after entering the repository root. Nested suites inherit this owner.
if [[ -z "${TS_TEST_BUILD_ROOT:-}" ]]; then
    TS_TEST_BUILD_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/tianshan-host.XXXXXX")
    export TS_TEST_BUILD_ROOT
    trap 'ts_host_status=$?; if (( ts_host_status == 0 )); then rm -rf -- "$TS_TEST_BUILD_ROOT"; else printf "Host-test artifacts retained: %s\n" "$TS_TEST_BUILD_ROOT" >&2; fi; exit "$ts_host_status"' EXIT
fi
