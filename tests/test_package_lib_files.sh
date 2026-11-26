#!/bin/sh
# The Keenetic lib directory is copied whole into the ipk. pkg.sh used to be
# left out because the Makefile named each helper. This checks the recipe and
# a synthetic ipk.
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

fail() {
    echo "package lib files: $*" >&2
    exit 1
}

require_recipe() {
    makefile="$1"
    copy_from="$2"
    call="$3"
    label="$4"
    grep -F "$copy_from" "$makefile" >/dev/null || fail "$label does not copy $copy_from"
    count=$(grep -cF "$call" "$makefile")
    [ "$count" -ge 2 ] || fail "$label install recipes call $call $count times"
}

keenetic_lib="$repo/packages/keenetic/keen-pbr-sx/files/opt/usr/lib/keen-pbr-sx"
openwrt_lib="$repo/packages/openwrt/keen-pbr-sx/files/usr/lib/keen-pbr-sx"
[ -f "$keenetic_lib/pkg.sh" ] || fail "pkg.sh is not in the Keenetic lib directory"
require_recipe \
    "$repo/packages/keenetic/keen-pbr-sx/Makefile" \
    "packages/keenetic/keen-pbr-sx/files/opt/usr/lib/keen-pbr-sx/." \
    "KeenPbr/InstallEntwareLib" \
    "Keenetic"
require_recipe \
    "$repo/packages/openwrt/keen-pbr-sx/Makefile" \
    "packages/openwrt/keen-pbr-sx/files/usr/lib/keen-pbr-sx/." \
    "KeenPbr/InstallOpenWrtLib" \
    "OpenWrt"

# Both variants must use the copy helper, not a private file list.
for variant in 'Package/keen-pbr-sx/install' 'Package/keen-pbr-sx-headless/install'; do
    awk -v start="$variant" '
        $0 ~ "^define " start { inside = 1 }
        inside { print }
        inside && $0 == "endef" { exit }
    ' "$repo/packages/keenetic/keen-pbr-sx/Makefile" | grep -q 'KeenPbr/InstallEntwareLib' \
        || fail "Keenetic $variant does not install the lib directory"
    awk -v start="$variant" '
        $0 ~ "^define " start { inside = 1 }
        inside { print }
        inside && $0 == "endef" { exit }
    ' "$repo/packages/openwrt/keen-pbr-sx/Makefile" | grep -q 'KeenPbr/InstallOpenWrtLib' \
        || fail "OpenWrt $variant does not install the lib directory"
done

make_ipk() {
    stage="$1"
    dest="$2"
    work=$(mktemp -d)
    tar -C "$stage" -czf "$work/data.tar.gz" .
    printf '2.0\n' > "$work/debian-binary"
    tar -C "$work" -czf "$work/control.tar.gz" debian-binary
    (
        cd "$work"
        ar r "$dest" debian-binary control.tar.gz data.tar.gz >/dev/null
    )
    rm -rf "$work"
}

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

stage="$tmp/stage"
mkdir -p "$stage/opt/usr/lib/keen-pbr-sx"
cp -a "$keenetic_lib/." "$stage/opt/usr/lib/keen-pbr-sx/"
cp "$repo/packages/common/cleanup-firewall.sh" "$stage/opt/usr/lib/keen-pbr-sx/cleanup-firewall.sh"
make_ipk "$stage" "$tmp/complete.ipk"
sh "$repo/build_scripts/check-package-data.sh" \
    "$tmp/complete.ipk" opt/usr/lib/keen-pbr-sx "$keenetic_lib"
rel="$tmp/rel.ipk"
cp "$tmp/complete.ipk" "$rel"
(
    cd "$tmp"
    sh "$repo/build_scripts/check-package-data.sh" rel.ipk opt/usr/lib/keen-pbr-sx "$keenetic_lib"
)

rm -f "$stage/opt/usr/lib/keen-pbr-sx/pkg.sh"
make_ipk "$stage" "$tmp/broken.ipk"
if sh "$repo/build_scripts/check-package-data.sh" \
    "$tmp/broken.ipk" opt/usr/lib/keen-pbr-sx "$keenetic_lib" >/dev/null 2>&1; then
    fail "checker accepted an ipk without pkg.sh"
fi

echo "package lib files: ok"
