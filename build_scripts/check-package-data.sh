#!/bin/sh
# Fail when a built ipk/apk data archive is missing a file from a source directory.
#
# Usage: check-package-data.sh <package> <archive-prefix> <source-dir>
# archive-prefix is the path inside data.tar (no leading slash), for example
# opt/usr/lib/keen-pbr-sx.
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

case "$package" in
    *.apk)
        # apk is a tar of the rootfs plus control files at the end.
        tar -tf "$package" > "$tmp/list" 2>/dev/null || fail "cannot list $package"
        ;;
    *)
        ar x --output "$tmp" "$package" || fail "cannot extract $package (ar)"
        data=""
        for candidate in data.tar.gz data.tar.xz data.tar.zst data.tar.bz2 data.tar; do
            if [ -f "$tmp/$candidate" ]; then
                data="$tmp/$candidate"
                break
            fi
        done
        [ -n "$data" ] || fail "no data.tar in $package"
        tar -tf "$data" > "$tmp/list" || fail "cannot list $data"
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
