#!/bin/sh
# Helpers for the Keenetic maintainer scripts. A real removal is the only
# time dnsmasq is unhooked. An upgrade and `opkg install --force-reinstall`
# keep the hook and start the services again from postinst.

KEEN_PBR_BIN="${KEEN_PBR_BIN:-/opt/usr/bin/keen-pbr-sx}"
KEEN_PBR_INIT="${KEEN_PBR_INIT:-/opt/etc/init.d/S80keen-pbr-sx}"
KEEN_PBR_HOOK="${KEEN_PBR_HOOK:-/opt/usr/lib/keen-pbr-sx/dnsmasq-hook.sh}"
DNSMASQ_INIT="${DNSMASQ_INIT:-/opt/etc/init.d/S56dnsmasq}"
DNSMASQ_STATE_FILE="${DNSMASQ_STATE_FILE:-/tmp/keen-pbr-sx.dnsmasq-before-pkg}"

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
# the maintainer script returns. Keenetic BusyBox cannot start a new session
# or ignore hangup, so the restart uses start-stop-daemon -b, or a subshell.
start_detached() {
    _bin="$1"
    shift
    if [ ! -x "$_bin" ]; then
        echo "init script is missing: $_bin" >&2
        return 1
    fi
    _ssd=""
    if [ -n "${START_STOP_DAEMON+x}" ]; then
        _ssd="$START_STOP_DAEMON"
    else
        for _cand in /opt/sbin/start-stop-daemon /opt/bin/start-stop-daemon start-stop-daemon; do
            if [ -x "$_cand" ]; then
                _ssd="$_cand"
                break
            fi
            if command -v "$_cand" >/dev/null 2>&1; then
                _ssd=$(command -v "$_cand")
                break
            fi
        done
    fi
    if [ -n "$_ssd" ]; then
        "$_ssd" -S -b -x "$_bin" -- "$@" && return 0
        echo "start-stop-daemon failed to start $_bin" >&2
        return 1
    fi
    # Wait until opkg drops its lock, then run the init script. The extra
    # subshell and closed stdio keep the job alive after this script exits.
    _arg1="${1:-}"
    _arg2="${2:-}"
    (
        KPBR_DETACH_BIN="$_bin" \
        KPBR_DETACH_ARG1="$_arg1" \
        KPBR_DETACH_ARG2="$_arg2" \
        KPBR_OPKG_LOCK="${OPKG_LOCK:-/opt/var/lock/opkg.lock}" \
        sh -c '
            i=0
            while [ "$i" -lt 30 ]; do
                if [ ! -e "$KPBR_OPKG_LOCK" ] && [ ! -e /var/lock/opkg.lock ] && [ ! -e /opt/tmp/opkg.lock ]; then
                    break
                fi
                sleep 1
                i=$((i + 1))
            done
            sleep 1
            if [ -n "$KPBR_DETACH_ARG2" ]; then
                "$KPBR_DETACH_BIN" "$KPBR_DETACH_ARG1" "$KPBR_DETACH_ARG2"
            elif [ -n "$KPBR_DETACH_ARG1" ]; then
                "$KPBR_DETACH_BIN" "$KPBR_DETACH_ARG1"
            else
                "$KPBR_DETACH_BIN"
            fi
        ' </dev/null >/dev/null 2>&1 &
    )
    return 0
}

# Copy a legacy keen-pbr file and rewrite its path strings to keen-pbr-sx.
rewrite_legacy_paths() {
    # Tokens first, longest path first. A later shorter pattern must not see
    # a path that was already rewritten to keen-pbr-sx.
    sed \
        -e 's#/opt/usr/lib/keen-pbr-sx#@@K1@@#g' \
        -e 's#/opt/usr/share/keen-pbr-sx#@@K2@@#g' \
        -e 's#/opt/usr/bin/keen-pbr-sx#@@K3@@#g' \
        -e 's#/opt/etc/keen-pbr-sx#@@K4@@#g' \
        -e 's#/opt/var/cache/keen-pbr-sx#@@K5@@#g' \
        -e 's#/opt/var/run/keen-pbr-sx#@@K6@@#g' \
        -e 's#/usr/lib/keen-pbr-sx#@@K7@@#g' \
        -e 's#/usr/sbin/keen-pbr-sx#@@K8@@#g' \
        -e 's#/etc/keen-pbr-sx#@@K9@@#g' \
        -e 's#/var/cache/keen-pbr-sx#@@K10@@#g' \
        -e 's#/var/run/keen-pbr-sx#@@K11@@#g' \
        -e 's#/tmp/keen-pbr-sx#@@K12@@#g' \
        -e 's#/opt/usr/lib/keen-pbr#@@N1@@#g' \
        -e 's#/opt/usr/share/keen-pbr#@@N2@@#g' \
        -e 's#/opt/usr/bin/keen-pbr#@@N3@@#g' \
        -e 's#/opt/etc/keen-pbr#@@N4@@#g' \
        -e 's#/opt/var/cache/keen-pbr#@@N5@@#g' \
        -e 's#/opt/var/run/keen-pbr#@@N6@@#g' \
        -e 's#/usr/lib/keen-pbr#@@N7@@#g' \
        -e 's#/usr/sbin/keen-pbr#@@N8@@#g' \
        -e 's#/etc/keen-pbr#@@N9@@#g' \
        -e 's#/var/cache/keen-pbr#@@N10@@#g' \
        -e 's#/var/run/keen-pbr#@@N11@@#g' \
        -e 's#/tmp/keen-pbr#@@N12@@#g' \
        -e 's#@@K1@@#/opt/usr/lib/keen-pbr-sx#g' \
        -e 's#@@N1@@#/opt/usr/lib/keen-pbr-sx#g' \
        -e 's#@@K2@@#/opt/usr/share/keen-pbr-sx#g' \
        -e 's#@@N2@@#/opt/usr/share/keen-pbr-sx#g' \
        -e 's#@@K3@@#/opt/usr/bin/keen-pbr-sx#g' \
        -e 's#@@N3@@#/opt/usr/bin/keen-pbr-sx#g' \
        -e 's#@@K4@@#/opt/etc/keen-pbr-sx#g' \
        -e 's#@@N4@@#/opt/etc/keen-pbr-sx#g' \
        -e 's#@@K5@@#/opt/var/cache/keen-pbr-sx#g' \
        -e 's#@@N5@@#/opt/var/cache/keen-pbr-sx#g' \
        -e 's#@@K6@@#/opt/var/run/keen-pbr-sx#g' \
        -e 's#@@N6@@#/opt/var/run/keen-pbr-sx#g' \
        -e 's#@@K7@@#/usr/lib/keen-pbr-sx#g' \
        -e 's#@@N7@@#/usr/lib/keen-pbr-sx#g' \
        -e 's#@@K8@@#/usr/sbin/keen-pbr-sx#g' \
        -e 's#@@N8@@#/usr/sbin/keen-pbr-sx#g' \
        -e 's#@@K9@@#/etc/keen-pbr-sx#g' \
        -e 's#@@N9@@#/etc/keen-pbr-sx#g' \
        -e 's#@@K10@@#/var/cache/keen-pbr-sx#g' \
        -e 's#@@N10@@#/var/cache/keen-pbr-sx#g' \
        -e 's#@@K11@@#/var/run/keen-pbr-sx#g' \
        -e 's#@@N11@@#/var/run/keen-pbr-sx#g' \
        -e 's#@@K12@@#/tmp/keen-pbr-sx#g' \
        -e 's#@@N12@@#/tmp/keen-pbr-sx#g'
}

copy_rewritten() {
    _src="$1"
    _dst="$2"
    _tmp="${_dst}.migrating"
    rewrite_legacy_paths < "$_src" > "$_tmp" || { rm -f "$_tmp"; return 1; }
    mv "$_tmp" "$_dst"
}

# Drop the keen-pbr 3.4 dnsmasq block. The keen-pbr-sx block uses a different
# marker, so a repeated run does not touch it.
strip_legacy_dnsmasq() {
    _conf="${DNSMASQ_CONF:-/opt/etc/dnsmasq.conf}"
    [ -f "$_conf" ] || return 0
    _tmp="${_conf}.keen-pbr-sx.migr"
    awk '
        $0 == "# BEGIN keen-pbr managed block" { skip = 1; next }
        skip && $0 == "# END keen-pbr managed block" { skip = 0; next }
        $0 == "# BEGIN keen-pbr fallback upstream (added on removal; replace or remove)" { skip2 = 1; next }
        skip2 && $0 == "# END keen-pbr fallback upstream" { skip2 = 0; next }
        skip || skip2 { next }
        $0 == "conf-script=/opt/usr/lib/keen-pbr/dnsmasq.sh dnsmasq-config-entry" { next }
        { print }
    ' "$_conf" > "$_tmp" || { rm -f "$_tmp"; return 1; }
    cat "$_tmp" > "$_conf" || { rm -f "$_tmp"; return 1; }
    rm -f "$_tmp"
}

stop_legacy_service() {
    _old_init="${KPBR_OLD_INIT:-/opt/etc/init.d/S80keen-pbr}"
    if [ -x "$_old_init" ]; then
        "$_old_init" stop || true
        chmod a-x "$_old_init" 2>/dev/null || true
    fi
    _ndm_routing="${KPBR_OLD_NDM_ROUTING:-/opt/etc/ndm/netfilter.d/50-keen-pbr-routing.sh}"
    _ndm_forward="${KPBR_OLD_NDM_FORWARD:-/opt/etc/ndm/netfilter.d/51-keen-pbr-forward.sh}"
    if [ -f "$_ndm_routing" ]; then
        chmod a-x "$_ndm_routing" 2>/dev/null || true
    fi
    if [ -f "$_ndm_forward" ]; then
        chmod a-x "$_ndm_forward" 2>/dev/null || true
    fi
}

# Idempotent: a stamp stops a later install from overwriting a config the user
# already has. Stopping the old init and removing the old dnsmasq block still
# run every time.
migrate_from_legacy() {
    _old="${KPBR_OLD_ETC:-/opt/etc/keen-pbr}"
    _new="${KPBR_NEW_ETC:-/opt/etc/keen-pbr-sx}"
    _stamp="${KPBR_MIGRATION_STAMP:-$_new/.migrated-from-keen-pbr}"
    if [ -d "$_old" ] && [ ! -f "$_stamp" ]; then
        mkdir -p "$_new" || return 1
        if [ -f "$_old/config.json" ]; then
            _copy_config=0
            if [ ! -f "$_new/config.json" ]; then
                _copy_config=1
            elif [ -f "$_new/config.example.json" ] && cmp -s "$_new/config.json" "$_new/config.example.json"; then
                _copy_config=1
            fi
            if [ "$_copy_config" = 1 ]; then
                copy_rewritten "$_old/config.json" "$_new/config.json" || return 1
            fi
        fi
        if [ -f "$_old/defaults" ] && [ -f "$_new/defaults" ]; then
            _old_raw=$(sed -n 's/^KEEN_PBR_RAW_PREROUTING="\(.*\)"/\1/p' "$_old/defaults" | head -n 1)
            _new_raw=$(sed -n 's/^KEEN_PBR_RAW_PREROUTING="\(.*\)"/\1/p' "$_new/defaults" | head -n 1)
            if [ -n "$_old_raw" ] && [ "$_old_raw" != "$_new_raw" ]; then
                copy_rewritten "$_old/defaults" "$_new/defaults" || return 1
            fi
        fi
        for _name in sing-box-links.json sing-box-profiles.json; do
            if [ -f "$_old/$_name" ] && [ ! -e "$_new/$_name" ]; then
                cp "$_old/$_name" "$_new/$_name" || return 1
            fi
        done
        for _src in "$_old"/sing-box-profile*.json; do
            [ -f "$_src" ] || continue
            _base=$(basename "$_src")
            [ -e "$_new/$_base" ] && continue
            cp "$_src" "$_new/$_base" || return 1
        done
        : > "$_stamp" || return 1
    fi
    stop_legacy_service
    strip_legacy_dnsmasq
}
