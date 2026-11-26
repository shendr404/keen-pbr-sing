#!/bin/sh

action="${ACTION:-${1:-}}"

logger -t "keen-pbr-sx" "[firewall.sh] Received action '$action'"

case "$action" in
    ""|includes|reload|restart)
        ;;
    *)
        exit 0
        ;;
esac

if [ -x /etc/init.d/keen-pbr-sx ] && /etc/init.d/keen-pbr-sx enabled; then
    logger -t "keen-pbr-sx" "[firewall.sh] Sending SIGUSR1 to keen-pbr-sx due to firewall action: ${action:-fw4-include}"
    /etc/init.d/keen-pbr-sx reapply_firewall
fi
