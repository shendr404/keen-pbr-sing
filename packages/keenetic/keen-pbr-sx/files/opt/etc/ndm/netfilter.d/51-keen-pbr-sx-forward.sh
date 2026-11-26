#!/opt/bin/sh
# NDM rebuilds the filter table on its own and leaves FORWARD policy DROP.
# Reinstall the interface-outbound accepts after that rebuild. mangle and nat
# are handled by 50-keen-pbr-sx-routing.sh; anything else is ignored.

[ "$table" = "filter" ] || exit 0
case "$type" in
    iptables|ip6tables) ;;
    *) exit 0 ;;
esac

SCRIPT="${KEEN_PBR_INTERFACE_FORWARD:-/opt/usr/lib/keen-pbr-sx/interface-forward.sh}"
[ -x "$SCRIPT" ] || exit 0
logger -t keen-pbr-sx "Refreshing interface FORWARD rules after netfilter change ($type)"
"$SCRIPT" apply "$type" >/dev/null 2>&1 || exit 0
exit 0
