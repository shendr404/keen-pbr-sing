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

# format is ar (OpenWrt ipk) or tar (Entware opkg-build: gzip of a tar).
# inner is gz, xz or zst: the compression of data.tar.* inside the wrapper.
make_ipk() {
    stage="$1"
    dest="$2"
    format="$3"
    inner="${4:-gz}"
    work=$(mktemp -d)
    case "$inner" in
        gz) tar -C "$stage" -czf "$work/data.tar.gz" . ;;
        xz) tar -C "$stage" -cJf "$work/data.tar.xz" . ;;
        zst) tar -C "$stage" --zstd -cf "$work/data.tar.zst" . ;;
        *) fail "unknown data.tar compression $inner" ;;
    esac
    printf '2.0\n' > "$work/debian-binary"
    tar -C "$work" -czf "$work/control.tar.gz" debian-binary
    case "$format" in
        ar)
            (
                cd "$work"
                ar r "$dest" debian-binary control.tar.gz data.tar.* >/dev/null
            )
            ;;
        tar)
            (
                cd "$work"
                tar -czf "$dest" ./debian-binary ./control.tar.gz ./data.tar.*
            )
            ;;
        *) fail "unknown ipk format $format" ;;
    esac
    rm -rf "$work"
}

assert_magic() {
    file="$1"
    expect="$2"
    got=$(od -An -tx1 -N6 "$file" | tr -d ' \n')
    case "$got" in
        "$expect"*) ;;
        *) fail "$file magic $got does not start with $expect" ;;
    esac
}

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

stage="$tmp/stage"
mkdir -p "$stage/opt/usr/lib/keen-pbr-sx"
cp -a "$keenetic_lib/." "$stage/opt/usr/lib/keen-pbr-sx/"
cp "$repo/packages/common/cleanup-firewall.sh" "$stage/opt/usr/lib/keen-pbr-sx/cleanup-firewall.sh"
make_ipk "$stage" "$tmp/complete-ar.ipk" ar gz
assert_magic "$tmp/complete-ar.ipk" 213c61726368
sh "$repo/build_scripts/check-package-data.sh" \
    "$tmp/complete-ar.ipk" opt/usr/lib/keen-pbr-sx "$keenetic_lib"
rel="$tmp/rel.ipk"
cp "$tmp/complete-ar.ipk" "$rel"
(
    cd "$tmp"
    sh "$repo/build_scripts/check-package-data.sh" rel.ipk opt/usr/lib/keen-pbr-sx "$keenetic_lib"
)

make_ipk "$stage" "$tmp/complete-tar.ipk" tar gz
assert_magic "$tmp/complete-tar.ipk" 1f8b
sh "$repo/build_scripts/check-package-data.sh" \
    "$tmp/complete-tar.ipk" opt/usr/lib/keen-pbr-sx "$keenetic_lib"

if command -v xz >/dev/null 2>&1; then
    make_ipk "$stage" "$tmp/complete-tar-xz.ipk" tar xz
    sh "$repo/build_scripts/check-package-data.sh" \
        "$tmp/complete-tar-xz.ipk" opt/usr/lib/keen-pbr-sx "$keenetic_lib"
    make_ipk "$stage" "$tmp/complete-ar-xz.ipk" ar xz
    sh "$repo/build_scripts/check-package-data.sh" \
        "$tmp/complete-ar-xz.ipk" opt/usr/lib/keen-pbr-sx "$keenetic_lib"
fi
if command -v zstd >/dev/null 2>&1; then
    make_ipk "$stage" "$tmp/complete-tar-zst.ipk" tar zst
    sh "$repo/build_scripts/check-package-data.sh" \
        "$tmp/complete-tar-zst.ipk" opt/usr/lib/keen-pbr-sx "$keenetic_lib"
fi

rm -f "$stage/opt/usr/lib/keen-pbr-sx/pkg.sh"
make_ipk "$stage" "$tmp/broken-ar.ipk" ar gz
if sh "$repo/build_scripts/check-package-data.sh" \
    "$tmp/broken-ar.ipk" opt/usr/lib/keen-pbr-sx "$keenetic_lib" >/dev/null 2>&1; then
    fail "checker accepted an ar ipk without pkg.sh"
fi
make_ipk "$stage" "$tmp/broken-tar.ipk" tar gz
if sh "$repo/build_scripts/check-package-data.sh" \
    "$tmp/broken-tar.ipk" opt/usr/lib/keen-pbr-sx "$keenetic_lib" >/dev/null 2>&1; then
    fail "checker accepted a tar.gz ipk without pkg.sh"
fi

echo "package lib files: ok"
