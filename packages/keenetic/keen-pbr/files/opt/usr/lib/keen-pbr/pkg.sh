#!/bin/sh
# Helpers for the Keenetic maintainer scripts. A real removal is the only
# time dnsmasq is unhooked. An upgrade and `opkg install --force-reinstall`
# keep the hook and start the services again from postinst.

KEEN_PBR_BIN="${KEEN_PBR_BIN:-/opt/usr/bin/keen-pbr}"
KEEN_PBR_INIT="${KEEN_PBR_INIT:-/opt/etc/init.d/S80keen-pbr}"
KEEN_PBR_HOOK="${KEEN_PBR_HOOK:-/opt/usr/lib/keen-pbr/dnsmasq-hook.sh}"
DNSMASQ_INIT="${DNSMASQ_INIT:-/opt/etc/init.d/S56dnsmasq}"
DNSMASQ_STATE_FILE="${DNSMASQ_STATE_FILE:-/tmp/keen-pbr.dnsmasq-before-pkg}"

opkg_command_line() {
    if [ -n "${OPKG_CMDLINE+x}" ]; then
        printf '%s\n' "$OPKG_CMDLINE"
        return 0
    fi
    _pid="$PPID"
    _hops=0
    while [ -n "$_pid" ] && [ "$_pid" != 0 ] && [ "$_pid" != 1 ] && [ "$_hops" -lt 8 ]; do
        if [ -r "/proc/$_pid/cmdline" ]; then
            tr '\0' ' ' < "/proc/$_pid/cmdline" 2>/dev/null || true
            printf '\n'
        fi
        _pid=$(sed -n 's/^PPid:[[:space:]]*//p' "/proc/$_pid/status" 2>/dev/null || echo 1)
        _hops=$((_hops + 1))
    done
}

opkg_is_reinstall() {
    _cmd=$(opkg_command_line)
    case "$_cmd" in
        *--force-reinstall*|*--force_reinstall*) return 0 ;;
    esac
    return 1
}

# 0: this prerm/postrm is a real removal. 1: upgrade or reinstall.
real_removal() {
    case "${1:-}" in
        upgrade|reinstall|failed-upgrade|abort-upgrade|abort-install|abort-remove)
            return 1
            ;;
    esac
    # "remove in-favour <package> <version>" replaces this package.
    [ "${2:-}" = "in-favour" ] && return 1
    case "${PKG_UPGRADE:-}" in
        1|yes|true) return 1 ;;
    esac
    opkg_is_reinstall && return 1
    case "$(basename "$0")" in
        postrm|*.postrm)
            # opkg deletes the files before postrm on removal. A binary that
            # is still there means the package is being replaced.
            [ -x "$KEEN_PBR_BIN" ] && return 1
            ;;
    esac
    return 0
}

# Remember whether Entware dnsmasq should be started again after install.
# Call this before anything stops dnsmasq.
record_dnsmasq_state() {
    _state=down
    if [ -x "$KEEN_PBR_HOOK" ] && "$KEEN_PBR_HOOK" alive >/dev/null 2>&1; then
        _state=running
    elif [ -f "$DNSMASQ_INIT" ] && ! grep -q '^ENABLED=no' "$DNSMASQ_INIT" 2>/dev/null; then
        _state=enabled
    fi
    mkdir -p "$(dirname "$DNSMASQ_STATE_FILE")" 2>/dev/null || true
    printf '%s\n' "$_state" > "$DNSMASQ_STATE_FILE" || true
}

dnsmasq_should_restart() {
    _state=$(cat "$DNSMASQ_STATE_FILE" 2>/dev/null || true)
    case "$_state" in
        running|enabled) return 0 ;;
        down) return 1 ;;
    esac
    # No record (a first install): start it when the init script is enabled.
    [ -f "$DNSMASQ_INIT" ] || return 1
    grep -q '^ENABLED=no' "$DNSMASQ_INIT" 2>/dev/null && return 1
    return 0
}

# The init script must outlive opkg. opkg tears down its process group when
# the maintainer script returns, which was killing a keen-pbr started in place.
start_detached() {
    _bin="$1"
    shift
    if [ ! -x "$_bin" ]; then
        echo "keen-pbr init script is missing: $_bin" >&2
        return 1
    fi
    _ssd=""
    for _cand in start-stop-daemon /opt/sbin/start-stop-daemon /opt/bin/start-stop-daemon; do
        if command -v "$_cand" >/dev/null 2>&1; then
            _ssd=$(command -v "$_cand")
            break
        fi
        [ -x "$_cand" ] && _ssd="$_cand" && break
    done
    if [ -n "$_ssd" ]; then
        "$_ssd" -S -b -x "$_bin" -- "$@" && return 0
    fi
    nohup setsid "$_bin" "$@" >/dev/null 2>&1 &
    return 0
}
