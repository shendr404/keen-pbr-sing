#!/bin/sh
# interface-forward.sh installs LAN<->TUN accepts only when they are missing,
# skips ip6tables when IPv6 is disabled, and removes the rules it added.
set -eu
repo=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
script="$repo/packages/keenetic/keen-pbr/files/opt/usr/lib/keen-pbr/interface-forward.sh"
hook="$repo/packages/keenetic/keen-pbr/files/opt/etc/ndm/netfilter.d/51-keen-pbr-forward.sh"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
fail() { printf 'FAIL: %s\n' "$1" >&2; exit 1; }

cat > "$tmp/iptables" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$FAKE_LOG"
if [ "$1" = "-I" ]; then
    shift 3
    printf '%s\n' "-C FORWARD $*" >> "$FAKE_HAVE"
    exit 0
fi
if [ "$1" = "-C" ]; then
    if grep -qxF -e "$*" "$FAKE_HAVE"; then exit 0; fi
    exit 1
fi
if [ "$1" = "-D" ]; then
    line="-C ${*#-D }"
    grep -qxF -e "$line" "$FAKE_HAVE" || exit 1
    grep -vxF -e "$line" "$FAKE_HAVE" > "$FAKE_HAVE.tmp" || true
    mv "$FAKE_HAVE.tmp" "$FAKE_HAVE"
    exit 0
fi
exit 0
EOF
chmod +x "$tmp/iptables"
: > "$tmp/log"
: > "$tmp/have"
export FAKE_LOG="$tmp/log" FAKE_HAVE="$tmp/have"
export IPTABLES="$tmp/iptables" IP6TABLES="$tmp/ip6tables"
printf '#!/bin/sh\nprintf %%s\\\\n "ip6 $*" >> "$FAKE_LOG"\nexit 0\n' > "$tmp/ip6tables"
chmod +x "$tmp/ip6tables"

cat > "$tmp/config.json" <<'EOF'
{
  "daemon": {"ipv6_enabled": false},
  "route": {"inbound_interfaces": ["br0"]},
  "outbounds": [
    {"tag": "proxy", "type": "interface", "interface": "sbtun", "gateway": "auto"}
  ]
}
EOF
export KEEN_PBR_CONFIG="$tmp/config.json"
sh "$script" apply || fail "apply failed"
grep -q -- '-C FORWARD -i br0 -o sbtun ' "$tmp/log" || fail "missing forward check"
grep -q -- '-I FORWARD 1 -i br0 -o sbtun ' "$tmp/log" || fail "missing forward insert"
grep -q -- '-i sbtun -o br0 -m conntrack --ctstate RELATED,ESTABLISHED' "$tmp/log" || fail "missing return path"
grep -q 'ip6 ' "$tmp/log" && fail "ip6tables ran while ipv6 is off"
: > "$tmp/log"
sh "$script" apply iptables || fail "second apply failed"
grep -q -- '-I FORWARD' "$tmp/log" && fail "inserted a rule that was already present"
sh "$script" remove iptables || fail "remove failed"
[ ! -s "$tmp/have" ] || fail "remove left a rule"

# The NDM hook ignores mangle and runs the script for filter/iptables.
export KEEN_PBR_INTERFACE_FORWARD="$script"
: > "$tmp/log"
type=mangle table=filter sh "$hook" || fail "hook mangle"
grep -q 'FORWARD' "$tmp/log" && fail "hook ran for mangle"
: > "$tmp/log"
type=iptables table=filter sh "$hook" || fail "hook filter"
grep -q -- '-C FORWARD -i br0 -o sbtun ' "$tmp/log" || fail "hook did not install rules"
printf 'ok\n'
