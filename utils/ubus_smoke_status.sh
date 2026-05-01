#!/bin/sh
# Smoke tests for ubus status functions exposed by mstpctl ubus daemon.
# Run from host: ./utils/ubus_smoke_status.sh [router_ip] [bridge_name]
# Defaults: router_ip=192.168.1.1 bridge_name=switch

set -eu

ROUTER_IP="${1:-192.168.1.1}"
BRIDGE_NAME="${2:-switch}"
SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null"

pass() { printf '[PASS] %s\n' "$1"; }
fail() { printf '[FAIL] %s\n' "$1"; exit 1; }
step() { printf '\n== %s ==\n' "$1"; }

run_ssh() {
    ssh $SSH_OPTS "root@${ROUTER_IP}" "$1"
}

assert_contains() {
    output="$1"
    needle="$2"
    msg="$3"
    printf '%s' "$output" | grep -q "$needle" || fail "$msg"
}

step "Connectivity"
run_ssh "true" >/dev/null 2>&1 || fail "Router ${ROUTER_IP} is unreachable via SSH"
pass "SSH connectivity"

step "Method availability"
METHODS="$(run_ssh "ubus -v list ustp")"
assert_contains "$METHODS" '"bridge_status"' "ustp.bridge_status is not exposed"
pass "ustp.bridge_status is exposed"

step "bridge_status happy path"
STATUS_JSON="$(run_ssh "ubus call ustp bridge_status '{\"name\":\"${BRIDGE_NAME}\"}'")" || \
    fail "bridge_status failed for bridge '${BRIDGE_NAME}'"
assert_contains "$STATUS_JSON" '"enabled"' "bridge_status response missing enabled"
assert_contains "$STATUS_JSON" '"stp_enabled"' "bridge_status response missing stp_enabled"
assert_contains "$STATUS_JSON" '"bridge_id"' "bridge_status response missing bridge_id"
assert_contains "$STATUS_JSON" '"designated_root"' "bridge_status response missing designated_root"
assert_contains "$STATUS_JSON" '"regional_root"' "bridge_status response missing regional_root"
assert_contains "$STATUS_JSON" '"protocol_version"' "bridge_status response missing protocol_version"
pass "bridge_status happy path"

step "bridge_status invalid argument"
set +e
INVALID_OUT="$(run_ssh "ubus call ustp bridge_status '{}'" 2>&1)"
INVALID_RC=$?
set -e
if [ "$INVALID_RC" -eq 0 ]; then
    fail "bridge_status with missing name unexpectedly succeeded"
fi
printf '%s' "$INVALID_OUT" | grep -qi 'Invalid argument' || \
    fail "bridge_status missing-name did not return Invalid argument"
pass "bridge_status invalid argument path"

step "bridge_status unknown bridge"
set +e
UNKNOWN_OUT="$(run_ssh "ubus call ustp bridge_status '{\"name\":\"nonexistent99\"}'" 2>&1)"
UNKNOWN_RC=$?
set -e
if [ "$UNKNOWN_RC" -eq 0 ]; then
    fail "bridge_status with unknown bridge unexpectedly succeeded"
fi
printf '%s' "$UNKNOWN_OUT" | grep -qi 'Unknown error' || \
    fail "bridge_status unknown bridge did not return Unknown error"
pass "bridge_status unknown bridge path"

printf '\nAll ubus status smoke tests passed for %s (bridge=%s).\n' "$ROUTER_IP" "$BRIDGE_NAME"
