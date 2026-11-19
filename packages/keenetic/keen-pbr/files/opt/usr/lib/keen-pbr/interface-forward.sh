#!/bin/sh
# FORWARD accepts for interface outbounds on Keenetic.
#
# NDM's filter FORWARD policy is DROP. Packets marked for a TUN such as
# sbtun are routed at the interface and then discarded, and the replies
# never come back. For each interface outbound this installs:
#   LAN -> iface          ACCEPT
#   iface -> LAN          ACCEPT if RELATED,ESTABLISHED
# The rules are inserted only when missing (-C, then -I). `remove` deletes
# every copy. ip6tables is used only when daemon.ipv6_enabled is not false.
# Non-filter tables are not this script's job.
#
#   interface-forward.sh apply [iptables|ip6tables]
#   interface-forward.sh remove [iptables|ip6tables]
# With no family, apply/remove runs every family that is enabled.

CONFIG="${KEEN_PBR_CONFIG:-/opt/etc/keen-pbr/config.json}"
IPTABLES="${IPTABLES:-iptables}"
IP6TABLES="${IP6TABLES:-ip6tables}"

log() { logger -t keen-pbr "interface-forward: $1" >/dev/null 2>&1 || true; }

safe_name() {
    case "$1" in
        ''|*[!A-Za-z0-9._-]*) return 1 ;;
    esac
}

ipv6_enabled() {
    [ -f "$CONFIG" ] || return 0
    awk '
        /"ipv6_enabled"[[:space:]]*:[[:space:]]*false/ { off = 1 }
        END { exit off ? 1 : 0 }
    ' "$CONFIG"
}

# Prints "iface <name>" and "lan <name>" lines.
read_config() {
    [ -f "$CONFIG" ] || return 0
    awk '
        function grab(line, key,    re, s) {
            re = "\"" key "\"[[:space:]]*:[[:space:]]*\""
            if (!match(line, re)) return ""
            s = substr(line, RSTART + RLENGTH)
            if (!match(s, /^[^"]*/)) return ""
            return substr(s, 1, RLENGTH)
        }
        function lans_from(line,    s, tok) {
            s = line
            sub(/^[^[]*\[/, "", s)
            sub(/\].*$/, "", s)
            while (match(s, /"[^"]+"/)) {
                tok = substr(s, RSTART + 1, RLENGTH - 2)
                if (tok != "") print "lan " tok
                s = substr(s, RSTART + RLENGTH)
            }
        }
        BEGIN { depth = 0; in_out = 0; in_lan = 0; type = ""; iface = "" }
        {
            if ($0 ~ /"inbound_interfaces"/) {
                lans_from($0)
                if ($0 !~ /\]/) in_lan = 1
            } else if (in_lan) {
                if ($0 ~ /\]/) { lans_from($0); in_lan = 0 }
                else lans_from($0)
            }
            if ($0 ~ /"outbounds"[[:space:]]*:/) in_out = 1
            opens = $0
            nopen = gsub(/\{/, "{", opens)
            if (in_out) {
                v = grab($0, "type"); if (v != "") type = v
                v = grab($0, "interface"); if (v != "") iface = v
            }
            depth += nopen
            closes = $0
            nclose = gsub(/\}/, "}", closes)
            depth -= nclose
            if (depth < 0) depth = 0
            if (in_out && depth <= 1 && type == "interface" && iface != "") {
                print "iface " iface
                type = ""; iface = ""
            }
            if (in_out && depth == 0) in_out = 0
        }
    ' "$CONFIG"
}

ensure_rule() {
    _cmd="$1"
    shift
    "$_cmd" -C FORWARD "$@" >/dev/null 2>&1 && return 0
    "$_cmd" -I FORWARD 1 "$@" || log "failed to insert: $_cmd -I FORWARD 1 $*"
}

delete_rule() {
    _cmd="$1"
    shift
    while "$_cmd" -C FORWARD "$@" >/dev/null 2>&1; do
        "$_cmd" -D FORWARD "$@" >/dev/null 2>&1 || break
    done
}

run_family() {
    _cmd="$1"
    _action="$2"
    command -v "$_cmd" >/dev/null 2>&1 || return 0
    _ifaces=""
    _lans=""
    _parsed="$(read_config)" || return 0
    _oldifs=$IFS
    IFS='
'
    for _row in $_parsed; do
        case "$_row" in
            "iface "*) _ifaces="$_ifaces ${_row#iface }" ;;
            "lan "*) _lans="$_lans ${_row#lan }" ;;
        esac
    done
    IFS=$_oldifs
    [ -n "$_lans" ] || _lans=" br0"
    for _iface in $_ifaces; do
        safe_name "$_iface" || continue
        for _lan in $_lans; do
            safe_name "$_lan" || continue
        [ "$_lan" = "$_iface" ] && continue
            _to="-i $_lan -o $_iface -m comment --comment keen-pbr:fwd:${_lan}:${_iface} -j ACCEPT"
            _back="-i $_iface -o $_lan -m conntrack --ctstate RELATED,ESTABLISHED -m comment --comment keen-pbr:fwd:${_iface}:${_lan} -j ACCEPT"
            # shellcheck disable=SC2086
            if [ "$_action" = remove ]; then
                delete_rule "$_cmd" $_to
                delete_rule "$_cmd" $_back
            else
                ensure_rule "$_cmd" $_to
                ensure_rule "$_cmd" $_back
            fi
        done
    done
}

ACTION="${1:-apply}"
FAMILY="${2:-}"
case "$ACTION" in
    apply|remove) ;;
    *) echo "usage: $0 apply|remove [iptables|ip6tables]" >&2; exit 64 ;;
esac

run_one() {
    case "$1" in
        iptables) run_family "$IPTABLES" "$ACTION" ;;
        ip6tables)
            ipv6_enabled || return 0
            run_family "$IP6TABLES" "$ACTION"
            ;;
    esac
}

if [ -n "$FAMILY" ]; then
    run_one "$FAMILY"
else
    run_one iptables
    run_one ip6tables
fi
exit 0
