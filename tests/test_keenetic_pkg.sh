#!/bin/sh
# Maintainer scripts must not unhook dnsmasq on upgrade or reinstall, and
# postinst must put the hook and both services back.
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
root=$(mktemp -d "${TMPDIR:-/tmp}/keen-pbr-sx-pkg.XXXXXX")
trap 'rm -rf "$root"' EXIT HUP INT TERM

fail() {
    echo "keenetic package test: $*" >&2
    exit 1
}

pkg="$repo/packages/keenetic/keen-pbr-sx/files"
hook="$pkg/opt/usr/lib/keen-pbr-sx/dnsmasq-hook.sh"
prerm="$pkg/prerm"
postrm="$pkg/postrm"
postinst="$pkg/postinst"

for f in "$hook" "$prerm" "$postrm" "$postinst" "$pkg/opt/usr/lib/keen-pbr-sx/pkg.sh"; do
    sh -n "$f" || fail "syntax error in $f"
done

bin="$root/bin"
mkdir -p "$bin" "$root/opt"
cat > "$bin/start-stop-daemon" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$SSD_LOG"
exit 0
EOF
# BusyBox on Keenetic has neither of these. If a script calls them, fail.
cat > "$bin/setsid" <<'EOF'
#!/bin/sh
printf 'setsid\n' >> "$FORBIDDEN_LOG"
exit 99
EOF
cat > "$bin/nohup" <<'EOF'
#!/bin/sh
printf 'nohup\n' >> "$FORBIDDEN_LOG"
exit 99
EOF
chmod +x "$bin/start-stop-daemon" "$bin/setsid" "$bin/nohup"
export PATH="$bin:$PATH"
export FORBIDDEN_LOG="$root/forbidden.log"
: > "$FORBIDDEN_LOG"
export SSD_LOG="$root/ssd.log"
export KEEN_PBR_PKG_LIB="$pkg/opt/usr/lib/keen-pbr-sx/pkg.sh"
export KEEN_PBR_BIN="$root/keen-pbr-sx"
export KEEN_PBR_INIT="$root/S80"
export KEEN_PBR_HOOK="$root/hook"
export DNSMASQ_INIT="$root/S56"
export DNSMASQ_STATE_FILE="$root/dns-state"
export DNSMASQ_CONF="$root/dnsmasq.conf"
export LOCK_DIR="$root/hook.lock"
export OPKG_CMDLINE=""
export START_STOP_DAEMON="$bin/start-stop-daemon"
export OPKG_LOCK="$root/no-opkg-lock"
export KPBR_OLD_ETC="$root/old-absent"
export KPBR_NEW_ETC="$root/new-absent"
export KPBR_OLD_INIT="$root/old-init-absent"
export KPBR_OLD_NDM_ROUTING="$root/ndm-r-absent"
export KPBR_OLD_NDM_FORWARD="$root/ndm-f-absent"
if grep -E 'setsid|nohup' "$pkg/opt/usr/lib/keen-pbr-sx/pkg.sh" "$postinst" "$pkg/postinst-headless"; then
    fail "maintainer scripts still depend on setsid or nohup"
fi

printf '#!/bin/sh\nprintf "%%s\\n" "$*" >> "%s"\nexit 0\n' "$root/calls" > "$KEEN_PBR_INIT"
printf '#!/bin/sh\nprintf "%%s\\n" "$*" >> "%s"\ncase "$1" in alive) exit 0 ;; port53-hint) exit 1 ;; esac\nexit 0\n' "$root/calls" > "$KEEN_PBR_HOOK"
printf '#!/bin/sh\nprintf "%%s\\n" "$*" >> "%s"\nexit 0\n' "$root/calls" > "$DNSMASQ_INIT"
chmod +x "$KEEN_PBR_INIT" "$KEEN_PBR_HOOK" "$DNSMASQ_INIT"
: > "$KEEN_PBR_BIN"
chmod +x "$KEEN_PBR_BIN"

calls() { cat "$root/calls" 2>/dev/null || true; }
reset() { : > "$root/calls"; : > "$SSD_LOG"; }

reset
sh "$prerm" upgrade 1.2.3
calls | grep -qx uninstall && fail "prerm upgrade unhooked dnsmasq"
calls | grep -qx stop || fail "prerm upgrade did not stop keen-pbr-sx"

reset
PKG_UPGRADE=1 sh "$prerm" remove
calls | grep -qx uninstall && fail "prerm PKG_UPGRADE unhooked dnsmasq"
unset PKG_UPGRADE

reset
OPKG_CMDLINE="opkg install --force-reinstall keen-pbr-sx" sh "$prerm" remove
calls | grep -qx uninstall && fail "prerm --force-reinstall unhooked dnsmasq"
grep -qx running "$DNSMASQ_STATE_FILE" || fail "prerm did not remember that dnsmasq was running"

reset
OPKG_CMDLINE="opkg remove keen-pbr-sx" sh "$prerm" remove
calls | grep -qx uninstall || fail "prerm remove did not unhook dnsmasq"
calls | grep -qx stop || fail "prerm remove did not stop keen-pbr-sx"

reset
rm -f "$KEEN_PBR_BIN"
out=$(OPKG_CMDLINE="opkg remove keen-pbr-sx" sh "$postrm" remove)
case "$out" in *has\ been\ removed*) ;; *) fail "postrm remove was silent: $out" ;; esac
calls | grep -qx uninstall || fail "postrm remove did not finish the unhook"

reset
: > "$KEEN_PBR_BIN"
chmod +x "$KEEN_PBR_BIN"
out=$(OPKG_CMDLINE="opkg remove keen-pbr-sx" sh "$postrm" remove)
case "$out" in *has\ been\ removed*) fail "postrm treated a remaining binary as removal" ;; esac
calls | grep -qx uninstall && fail "postrm unhooked while the binary remains"

reset
out=$(PKG_UPGRADE=1 sh "$postrm" upgrade 9)
case "$out" in *has\ been\ removed*) fail "postrm upgrade announced a removal" ;; esac

# postinst uses the real hook so the fallback block is actually removed.
export KEEN_PBR_HOOK="$hook"
printf 'no-resolv\n\n# BEGIN keen-pbr-sx fallback upstream (added on removal; replace or remove)\nserver=8.8.8.8\nserver=8.8.4.4\n# END keen-pbr-sx fallback upstream\n' > "$DNSMASQ_CONF"
printf 'server=8.8.8.8\nserver=8.8.4.4\n' > "$root/fallback.conf"
export FALLBACK_CONF="$root/fallback.conf"
printf 'running\n' > "$DNSMASQ_STATE_FILE"
reset
out=$(NDNPROXY_HOLDS_53=1 DNS_OVERRIDE=off sh "$postinst")
grep -q 'BEGIN keen-pbr-sx managed block' "$DNSMASQ_CONF" || fail "postinst did not restore the managed block"
grep -q 'fallback upstream' "$DNSMASQ_CONF" && fail "postinst left the fallback block"
grep -q -- "-S -b -x $DNSMASQ_INIT -- restart" "$SSD_LOG" || fail "postinst did not detach dnsmasq via start-stop-daemon: $(cat "$SSD_LOG")"
grep -q -- "-S -b -x $KEEN_PBR_INIT -- restart" "$SSD_LOG" || fail "postinst did not detach keen-pbr-sx via start-stop-daemon: $(cat "$SSD_LOG")"
[ ! -s "$FORBIDDEN_LOG" ] || fail "postinst called setsid or nohup: $(cat "$FORBIDDEN_LOG")"
case "$out" in
    *ndnproxy*port\ 53*dns-override*system\ configuration\ save*) ;;
    *) fail "postinst did not explain the port 53 conflict: $out" ;;
esac

printf 'down\n' > "$DNSMASQ_STATE_FILE"
reset
sh "$postinst" >/dev/null
grep -q -- "-x $DNSMASQ_INIT" "$SSD_LOG" && fail "postinst restarted dnsmasq that was not enabled"
grep -q -- "-x $KEEN_PBR_INIT -- restart" "$SSD_LOG" || fail "postinst skipped keen-pbr-sx when dnsmasq stayed down"

# No start-stop-daemon: the restart is a background shell that waits out the opkg lock.
export START_STOP_DAEMON=""
printf 'running\n' > "$DNSMASQ_STATE_FILE"
: > "$root/opkg.lock"
export OPKG_LOCK="$root/opkg.lock"
reset
sh "$postinst" >/dev/null
sleep 1
[ ! -s "$root/calls" ] || fail "fallback restarted before opkg released the lock: $(calls)"
rm -f "$root/opkg.lock"
sleep 3
calls | grep -qx restart || fail "fallback did not restart dnsmasq after the lock: $(calls)"
grep -q "$KEEN_PBR_INIT" "$root/calls" || true
# Both init scripts log every argument on one line. dnsmasq and keen-pbr-sx each print "restart".
[ "$(calls | grep -cx restart)" = 2 ] || fail "fallback did not restart both services: $(calls)"
[ ! -s "$FORBIDDEN_LOG" ] || fail "fallback called setsid or nohup: $(cat "$FORBIDDEN_LOG")"
[ ! -s "$SSD_LOG" ] || fail "fallback used start-stop-daemon: $(cat "$SSD_LOG")"

# Migration from keen-pbr 3.4.1: copy a stock config, keep a custom one, copy
# the link store and profiles once, stop the old init, drop only the old
# dnsmasq block. A second run does not overwrite the copied config.
export START_STOP_DAEMON="$bin/start-stop-daemon"
export OPKG_LOCK="$root/no-opkg-lock"
export KEEN_PBR_HOOK="$root/hook"
old="$root/mig-old"
new="$root/mig-new"
mkdir -p "$old" "$new" "$root/ndm"
export KPBR_OLD_ETC="$old"
export KPBR_NEW_ETC="$new"
export KPBR_OLD_INIT="$root/S80old"
export KPBR_OLD_NDM_ROUTING="$root/ndm/50-keen-pbr-routing.sh"
export KPBR_OLD_NDM_FORWARD="$root/ndm/51-keen-pbr-forward.sh"
printf '#!/bin/sh\nprintf "old-%%s\\n" "$1" >> "%s"\nexit 0\n' "$root/calls" > "$KPBR_OLD_INIT"
chmod +x "$KPBR_OLD_INIT" "$root/ndm/50-keen-pbr-routing.sh" "$root/ndm/51-keen-pbr-forward.sh" 2>/dev/null || true
printf '#!/bin/sh\nexit 0\n' > "$KPBR_OLD_NDM_ROUTING"
printf '#!/bin/sh\nexit 0\n' > "$KPBR_OLD_NDM_FORWARD"
chmod +x "$KPBR_OLD_NDM_ROUTING" "$KPBR_OLD_NDM_FORWARD"
printf '%s\n' 'KEEN_PBR="/opt/usr/bin/keen-pbr"' 'KEEN_PBR_RAW_PREROUTING="enable"' > "$old/defaults"
printf '%s\n' 'KEEN_PBR="/opt/usr/bin/keen-pbr-sx"' 'KEEN_PBR_RAW_PREROUTING="auto"' > "$new/defaults"
printf '%s\n' '{"device_name":"from-3.4.1","cache_dir":"/opt/var/cache/keen-pbr","already":"/opt/etc/keen-pbr-sx/keep"}' > "$old/config.json"
printf '%s\n' '{"stock":true}' > "$new/config.json"
cp "$new/config.json" "$new/config.example.json"
printf '%s\n' '{"subscriptions":[{"id":"sub1"}]}' > "$old/sing-box-links.json"
printf '%s\n' '{"profiles":["node-a"]}' > "$old/sing-box-profiles.json"
printf '%s\n' '# BEGIN keen-pbr managed block' \
    'conf-script=/opt/usr/lib/keen-pbr/dnsmasq.sh dnsmasq-config-entry' \
    '# END keen-pbr managed block' \
    '# BEGIN keen-pbr-sx managed block' \
    'conf-script=/opt/usr/lib/keen-pbr-sx/dnsmasq.sh dnsmasq-config-entry' \
    '# END keen-pbr-sx managed block' > "$DNSMASQ_CONF"
printf 'running\n' > "$DNSMASQ_STATE_FILE"
reset
sh "$postinst" >/dev/null
grep -q 'from-3.4.1' "$new/config.json" || fail "migration did not copy the old config"
grep -q '/opt/var/cache/keen-pbr-sx' "$new/config.json" || fail "migration did not rewrite cache_dir: $(cat "$new/config.json")"
grep -q '/opt/var/cache/keen-pbr"' "$new/config.json" && fail "migration doubled or left the old cache_dir: $(cat "$new/config.json")"
grep -q '/opt/etc/keen-pbr-sx/keep' "$new/config.json" || fail "migration rewrote an existing keen-pbr-sx path: $(cat "$new/config.json")"
grep -q 'KEEN_PBR_RAW_PREROUTING="enable"' "$new/defaults" || fail "migration did not keep the RAW mode"
grep -q '/opt/usr/bin/keen-pbr-sx' "$new/defaults" || fail "migration did not rewrite the binary path in defaults"
grep -q 'sub1' "$new/sing-box-links.json" || fail "migration did not copy the link store"
grep -q 'node-a' "$new/sing-box-profiles.json" || fail "migration did not copy sing-box profiles"
grep -q 'BEGIN keen-pbr managed block' "$DNSMASQ_CONF" && fail "migration left the old dnsmasq block"
grep -q 'BEGIN keen-pbr-sx managed block' "$DNSMASQ_CONF" || fail "migration removed the new dnsmasq block"
grep -q 'conf-script=/opt/usr/lib/keen-pbr/dnsmasq.sh' "$DNSMASQ_CONF" && fail "migration left the old conf-script"
calls | grep -qx 'old-stop' || fail "migration did not stop the old init: $(calls)"
[ ! -x "$KPBR_OLD_INIT" ] || fail "migration did not disable the old init"
[ ! -x "$KPBR_OLD_NDM_ROUTING" ] || fail "migration did not disable the old routing hook"
[ -f "$new/.migrated-from-keen-pbr" ] || fail "migration did not write the stamp"
[ ! -s "$FORBIDDEN_LOG" ] || fail "migration called setsid or nohup"

printf '%s\n' '{"device_name":"edited"}' > "$new/config.json"
printf '%s\n' '{"subscriptions":[{"id":"changed"}]}' > "$old/sing-box-links.json"
chmod +x "$KPBR_OLD_INIT"
printf '%s\n' '# BEGIN keen-pbr managed block' '# END keen-pbr managed block' >> "$DNSMASQ_CONF"
reset
sh "$postinst" >/dev/null
grep -q 'edited' "$new/config.json" || fail "second migration overwrote the config"
grep -q 'from-3.4.1' "$new/config.json" && fail "second migration restored the old config"
grep -q 'sub1' "$new/sing-box-links.json" || fail "second migration replaced the link store"
grep -q 'BEGIN keen-pbr managed block' "$DNSMASQ_CONF" && fail "second migration left a returned old block"
calls | grep -qx 'old-stop' || fail "second migration did not stop the old init again"
[ ! -s "$FORBIDDEN_LOG" ] || fail "second migration called setsid or nohup"

# A config that is already the user's is not replaced on the first run.
cust_old="$root/cust-old"
cust_new="$root/cust-new"
mkdir -p "$cust_old" "$cust_new"
export KPBR_OLD_ETC="$cust_old"
export KPBR_NEW_ETC="$cust_new"
export KPBR_OLD_INIT="$root/old-init-absent"
printf '%s\n' '{"device_name":"old"}' > "$cust_old/config.json"
printf '%s\n' '{"device_name":"custom"}' > "$cust_new/config.json"
printf '%s\n' '{"stock":true}' > "$cust_new/config.example.json"
printf '%s\n' '{"subscriptions":[{"id":"keep"}]}' > "$cust_old/sing-box-links.json"
reset
sh "$postinst" >/dev/null
grep -q 'custom' "$cust_new/config.json" || fail "migration overwrote a custom config"
grep -q 'keep' "$cust_new/sing-box-links.json" || fail "migration skipped the link store when the config was custom"
[ -f "$cust_new/.migrated-from-keen-pbr" ] || fail "custom config did not stamp the migration"

echo "keenetic package test: ok"
