#!/bin/sh
# Fail when a built ipk/apk data archive is missing a file from a source directory.
#
# Usage: check-package-data.sh <package> <archive-prefix> <source-dir>
# archive-prefix is the path inside the payload (no leading slash), for example
# opt/usr/lib/keen-pbr-sx.
#
# Entware opkg-build writes a gzip tar whose members are ./debian-binary,
# ./control.tar.gz and ./data.tar.gz. OpenWrt ipk is an ar archive with the
# same members. The payload itself may be data.tar, data.tar.gz, data.tar.xz,
# data.tar.zst or data.tar.bz2. An apk is a tar of the root filesystem.
set -eu

package="${1:?package path}"
prefix="${2:?archive prefix}"
source_dir="${3:?source directory}"

fail() {
    echo "check-package-data: $*" >&2
    exit 1
}

[ -f "$package" ] || fail "package not found: $package"
[ -d "$source_dir" ] || fail "source directory not found: $source_dir"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
case "$package" in
    /*) ;;
    *)
        dir=$(CDPATH= cd -- "$(dirname -- "$package")" && pwd)
        package="$dir/$(basename -- "$package")"
        ;;
esac

# First six bytes, lowercase hex, no separators.
magic=$(od -An -tx1 -N6 "$package" | tr -d ' \n')

# List a tar (compressed or not). GNU tar picks the compression from the bytes.
list_tar() {
    tar -tf "$1" > "$2" || fail "cannot list $1"
}

# Top-level data.tar* member, if this archive is an ipk wrapper.
data_member() {
    grep -E '^(./)?data\.tar(\.(gz|xz|zst|bz2))?$' "$1" | head -n 1 || true
}

list_wrapped_tar() {
    outer="$1"
    list_tar "$outer" "$tmp/outer"
    member=$(data_member "$tmp/outer")
    if [ -n "$member" ]; then
        tar -xOf "$outer" "$member" > "$tmp/payload" || fail "cannot extract $member from $outer"
        list_tar "$tmp/payload" "$tmp/list"
    else
        cp "$tmp/outer" "$tmp/list"
    fi
}

case "$magic" in
    213c61726368*)
        # !<arch> — OpenWrt ipk and Debian-style ar packages.
        mkdir -p "$tmp/ar"
        ar x --output "$tmp/ar" "$package" || fail "cannot extract $package (ar)"
        payload=""
        for candidate in data.tar.gz data.tar.xz data.tar.zst data.tar.bz2 data.tar; do
            if [ -f "$tmp/ar/$candidate" ]; then
                payload="$tmp/ar/$candidate"
                break
            fi
        done
        [ -n "$payload" ] || fail "no data.tar in $package"
        list_tar "$payload" "$tmp/list"
        ;;
    1f8b*|28b52ffd*|fd377a585a00*|425a68*)
        # gzip (1f8b), zstd (28b52ffd), xz (fd377a585a00) or bzip2 (425a68).
        # Entware ipk is gzip of a tar that holds data.tar*. apk is the rootfs tar.
        list_wrapped_tar "$package"
        ;;
    *)
        if tar -tf "$package" > "$tmp/outer" 2>/dev/null; then
            member=$(data_member "$tmp/outer")
            if [ -n "$member" ]; then
                tar -xOf "$package" "$member" > "$tmp/payload" || fail "cannot extract $member from $package"
                list_tar "$tmp/payload" "$tmp/list"
            else
                cp "$tmp/outer" "$tmp/list"
            fi
        else
            fail "unrecognized package format (magic $magic): $package"
        fi
        ;;
esac

missing=0
found=0
for file in "$source_dir"/*; do
    [ -f "$file" ] || continue
    found=$((found + 1))
    base=$(basename "$file")
    if ! grep -F "${prefix}/${base}" "$tmp/list" >/dev/null; then
        echo "check-package-data: missing ${prefix}/${base} in $package" >&2
        missing=1
    fi
done

[ "$found" -gt 0 ] || fail "source directory is empty: $source_dir"
[ "$missing" -eq 0 ] || fail "package $package is incomplete"

echo "check-package-data: $package contains $found files from ${prefix}"
