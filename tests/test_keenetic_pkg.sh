#!/bin/sh
# Maintainer scripts must not unhook dnsmasq on upgrade or reinstall, and
# postinst must put the hook and both services back.
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
root=$(mktemp -d "${TMPDIR:-/tmp}/keen-pbr-pkg.XXXXXX")
trap 'rm -rf "$root"' EXIT HUP INT TERM

fail() {
    echo "keenetic package test: $*" >&2
    exit 1
}

pkg="$repo/packages/keenetic/keen-pbr/files"
hook="$pkg/opt/usr/lib/keen-pbr/dnsmasq-hook.sh"
prerm="$pkg/prerm"
postrm="$pkg/postrm"
postinst="$pkg/postinst"

for f in "$hook" "$prerm" "$postrm" "$postinst" "$pkg/opt/usr/lib/keen-pbr/pkg.sh"; do
    sh -n "$f" || fail "syntax error in $f"
done

bin="$root/bin"
mkdir -p "$bin" "$root/opt"
cat > "$bin/start-stop-daemon" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$SSD_LOG"
exit 0
EOF
chmod +x "$bin/start-stop-daemon"
export PATH="$bin:$PATH"
export SSD_LOG="$root/ssd.log"
export KEEN_PBR_PKG_LIB="$pkg/opt/usr/lib/keen-pbr/pkg.sh"
export KEEN_PBR_BIN="$root/keen-pbr"
export KEEN_PBR_INIT="$root/S80"
export KEEN_PBR_HOOK="$root/hook"
export DNSMASQ_INIT="$root/S56"
export DNSMASQ_STATE_FILE="$root/dns-state"
export DNSMASQ_CONF="$root/dnsmasq.conf"
export LOCK_DIR="$root/hook.lock"
export OPKG_CMDLINE=""

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
calls | grep -qx stop || fail "prerm upgrade did not stop keen-pbr"

reset
PKG_UPGRADE=1 sh "$prerm" remove
calls | grep -qx uninstall && fail "prerm PKG_UPGRADE unhooked dnsmasq"
unset PKG_UPGRADE

reset
OPKG_CMDLINE="opkg install --force-reinstall keen-pbr" sh "$prerm" remove
calls | grep -qx uninstall && fail "prerm --force-reinstall unhooked dnsmasq"
grep -qx running "$DNSMASQ_STATE_FILE" || fail "prerm did not remember that dnsmasq was running"

reset
OPKG_CMDLINE="opkg remove keen-pbr" sh "$prerm" remove
calls | grep -qx uninstall || fail "prerm remove did not unhook dnsmasq"
calls | grep -qx stop || fail "prerm remove did not stop keen-pbr"

reset
rm -f "$KEEN_PBR_BIN"
out=$(OPKG_CMDLINE="opkg remove keen-pbr" sh "$postrm" remove)
case "$out" in *has\ been\ removed*) ;; *) fail "postrm remove was silent: $out" ;; esac
calls | grep -qx uninstall || fail "postrm remove did not finish the unhook"

reset
: > "$KEEN_PBR_BIN"
chmod +x "$KEEN_PBR_BIN"
out=$(OPKG_CMDLINE="opkg remove keen-pbr" sh "$postrm" remove)
case "$out" in *has\ been\ removed*) fail "postrm treated a remaining binary as removal" ;; esac
calls | grep -qx uninstall && fail "postrm unhooked while the binary remains"

reset
out=$(PKG_UPGRADE=1 sh "$postrm" upgrade 9)
case "$out" in *has\ been\ removed*) fail "postrm upgrade announced a removal" ;; esac

# postinst uses the real hook so the fallback block is actually removed.
export KEEN_PBR_HOOK="$hook"
printf 'no-resolv\n\n# BEGIN keen-pbr fallback upstream (added on removal; replace or remove)\nserver=8.8.8.8\nserver=8.8.4.4\n# END keen-pbr fallback upstream\n' > "$DNSMASQ_CONF"
printf 'server=8.8.8.8\nserver=8.8.4.4\n' > "$root/fallback.conf"
export FALLBACK_CONF="$root/fallback.conf"
printf 'running\n' > "$DNSMASQ_STATE_FILE"
reset
out=$(NDNPROXY_HOLDS_53=1 DNS_OVERRIDE=off sh "$postinst")
grep -q 'BEGIN keen-pbr managed block' "$DNSMASQ_CONF" || fail "postinst did not restore the managed block"
grep -q 'fallback upstream' "$DNSMASQ_CONF" && fail "postinst left the fallback block"
calls | grep -qx restart || fail "postinst did not restart dnsmasq"
grep -q 'restart' "$SSD_LOG" || fail "postinst did not start keen-pbr detached: $(cat "$SSD_LOG")"
case "$out" in
    *ndnproxy*port\ 53*dns-override*system\ configuration\ save*) ;;
    *) fail "postinst did not explain the port 53 conflict: $out" ;;
esac

printf 'down\n' > "$DNSMASQ_STATE_FILE"
reset
sh "$postinst" >/dev/null
calls | grep -qx restart && fail "postinst restarted dnsmasq that was not enabled"

echo "keenetic package test: ok"
