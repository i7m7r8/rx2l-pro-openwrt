#!/bin/sh
# Does /bin/decry_firm create /tmp/UPGRADE_DECRY_FAIL_FLAG for a *plaintext*
# (already-decrypted / hand-built) image, and NOT for a genuine encrypted
# TDE01 upload?
#
# Read-only w.r.t. the device dump: everything happens in $W.
#   ROOTFS = extracted stock rootfs (qemu -L sysroot)
#   W      = scratch dir
set -e
ROOTFS=/tmp/rx_dump/rootfs/x
W=/tmp/veto
ENC=/root/rx2lpro/work/doc/V16.03.51.17/US_RX2LProV1.0re_V16.03.51.17_multi_TDE01.bin
PLAIN=/root/rx2lpro/dec/RX2LPro_51.17_decrypted.bin

rm -rf $W; mkdir -p $W/a $W/b
cp $ENC $W/a/image.bin
cp $PLAIN $W/b/image.bin

echo "=== sha256 before ==="
sha256sum $W/a/image.bin $W/b/image.bin

echo
echo "=== A) genuine ENCRYPTED TDE01 upload (positive control) ==="
set +e
qemu-mipsel-static -L $ROOTFS /bin/decry_firm $W/a/image.bin $W/a/UPGRADE_DECRY_FAIL_FLAG
echo "rc=$?"
set -e
echo -n "flag exists? "; [ -e $W/a/UPGRADE_DECRY_FAIL_FLAG ] && echo YES || echo NO
sha256sum $W/a/image.bin

echo
echo "=== B) PLAINTEXT image (our hand-built / already-decrypted case) ==="
set +e
qemu-mipsel-static -L $ROOTFS /bin/decry_firm $W/b/image.bin $W/b/UPGRADE_DECRY_FAIL_FLAG
echo "rc=$?"
set -e
echo -n "flag exists? "; [ -e $W/b/UPGRADE_DECRY_FAIL_FLAG ] && echo YES || echo NO
ls -la $W/b/ 2>/dev/null
sha256sum $W/b/image.bin

echo
echo "=== C) same as B but flag path is the literal stock path ==="
rm -f /tmp/UPGRADE_DECRY_FAIL_FLAG
cp $PLAIN /var/image 2>/dev/null || cp $PLAIN $W/plain-copy.bin
if [ -f /var/image ]; then
  set +e
  qemu-mipsel-static -L $ROOTFS /bin/decry_firm /var/image /tmp/UPGRADE_DECRY_FAIL_FLAG
  echo "rc=$?"
  set -e
  echo -n "/tmp/UPGRADE_DECRY_FAIL_FLAG exists? "
  [ -e /tmp/UPGRADE_DECRY_FAIL_FLAG ] && echo YES || echo NO
  ls -la /tmp/UPGRADE_DECRY_FAIL_FLAG 2>/dev/null
  rm -f /tmp/UPGRADE_DECRY_FAIL_FLAG /var/image
fi
