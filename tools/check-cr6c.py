#!/usr/bin/env python3
"""check-cr6c - verify a built image against the RX2L Pro container spec.

Run against anything this repo produces before it goes near a router. Every
number below was established from the device's own mtd3 dump and two
decrypted vendor images; see notes/cr6c-container.md.

    python3 tools/check-cr6c.py openwrt/bin/targets/realtek/rtl8197f/*factory-cr6c.bin
    python3 tools/check-cr6c.py --tenda openwrt/bin/targets/realtek/rtl8197f/*tenda-plaintext.bin
    python3 tools/check-cr6c.py --stock /path/to/mtd3_KernelFS.bin

Exit 0 = every check passed. This tool never writes anything.
"""
import argparse
import struct
import sys
import zlib

HDR = 16
TENDA_HDR = 0x40
ERASE = 0x10000
SQUASHFS_MAGIC = b'hsqs'
SQUASHFS_MAGIC_VENDOR = b'nice'
ROOTFS_SIZES = {0x3c0800, 0x3c0c00}          # vendor kernel payload sizes, for context only
# fw.product.id for this router: from the device's own CFG partition and
# confirmed in the genuine decrypted image at +0x24 (notes/webui-upgrade-
# contract.md, check_fw_valid). A product constant, not a per-unit secret.
RX2L_PRO_PRODUCT_ID = 0x00031053


def cvimg_checksum(buf):
    """cvimg.c calculate_checksum(): 16-bit big-endian word sum, htons(~sum + 1)."""
    s = 0
    n = len(buf)
    for i in range(0, n - 1, 2):
        s = (s + ((buf[i] << 8) | buf[i + 1])) & 0xFFFF
    if n % 2:
        s = (s + (buf[n - 1] << 8)) & 0xFFFF
    return struct.pack('>H', (~s + 1) & 0xFFFF)


class Report:
    def __init__(self):
        self.fails = []
        self.warns = []

    def check(self, ok, label, detail=''):
        print('  [%s] %s%s' % ('PASS' if ok else 'FAIL', label,
                               (' - ' + detail) if detail else ''))
        if not ok:
            self.fails.append(label)
        return ok

    def warn(self, label, detail=''):
        print('  [WARN] %s%s' % (label, (' - ' + detail) if detail else ''))
        self.warns.append(label)


def parse_container(data, off, rep, want_start=None, allow_cr6b=False):
    """Validate the cvimg header + checksum + rootfs placement at data[off:]."""
    sig = data[off:off + 4]
    # Our stage-1 accepts `cr6c` and `cs6c` only; `cr6b` is the D-Link DIR-842
    # signature and this loader answers it with "No sys signature at %X!" and
    # drops to the command monitor. Accepting it here (as this check used to)
    # would bless an image the router refuses to boot, which is the one thing a
    # build gate must never do. Other boards' dumps can still be inspected with
    # --allow-cr6b.
    allowed = (b'cr6c', b'cs6c') + ((b'cr6b',) if allow_cr6b else ())
    if not rep.check(sig in allowed, 'container signature',
                     '%r%s' % (sig, ' (cr6b is not bootable by this loader)'
                               if sig == b'cr6b' and not allow_cr6b else '')):
        return None
    start, burn, ln = struct.unpack('>III', data[off + 4:off + 16])
    print('       signature=%s start_addr=%#x burn_addr=%#x len=%#x payload=%#x' %
          (sig.decode('latin1'), start, burn, ln, ln - 2))

    if want_start is not None:
        rep.check(start == want_start, 'start_addr matches profile',
                  '%#x != %#x' % (start, want_start) if start != want_start else '')
    payload = ln - 2
    ck_off = off + HDR + payload
    rep.check(payload > 0 and ck_off + 2 <= len(data),
              'declared payload fits in the file',
              'payload=%#x file=%#x' % (payload, len(data)))
    got = cvimg_checksum(data[off + HDR:ck_off])
    want = data[ck_off:ck_off + 2]
    rep.check(got == want, 'cvimg checksum over the kernel payload',
              'file has %s, recomputed %s' % (want.hex(), got.hex()))

    exact = off + HDR + ln                            # what the vendor kernel uses
    root_off = (exact + 3) & ~3                       # ALIGN(exact, 4)
    direct = data[root_off:root_off + 4]
    exact_magic = data[exact:exact + 4]
    if exact_magic in (SQUASHFS_MAGIC, SQUASHFS_MAGIC_VENDOR):
        rep.check(True, 'rootfs at the exact offset 16+len = %#x' % exact,
                  'magic=%s' % repr(exact_magic))
        root_off = exact
    elif direct not in (SQUASHFS_MAGIC, SQUASHFS_MAGIC_VENDOR):
        # Neither candidate hit. mtdsplit_cvimg's fallback scans from
        # CVIMG_MIN_ROOTFS_OFFSET (0xa0000) in 4 KiB steps, which cannot see a
        # superblock at an offset like 0x3c0812 - that is precisely why the
        # driver here was patched to try the exact 16+len first (see
        # files/target/linux/generic/files/drivers/mtd/mtdsplit/mtdsplit_cvimg.c).
        # Reaching this branch means the image really is not splittable.
        found = None
        step = 0x1000
        start = max(0xa0000, off)
        for cand in range(start, len(data) - 4, step):
            if data[cand:cand + 4] in (SQUASHFS_MAGIC, SQUASHFS_MAGIC_VENDOR):
                found = cand
                break
        if found is None:
            rep.check(False, 'rootfs superblock found (direct then scan)',
                      'nothing at %#x and no magic on a 4 KiB scan from %#x' %
                      (root_off, start))
            return None
        rep.check(True, 'rootfs superblock found by the mtdsplit scan path',
                  'direct %#x held %s; scan hit at %#x' %
                  (root_off, repr(direct), found))
        root_off = found
    magic = data[root_off:root_off + 4]
    vendor = magic == SQUASHFS_MAGIC_VENDOR
    rep.check(magic in (SQUASHFS_MAGIC, SQUASHFS_MAGIC_VENDOR),
              'rootfs superblock located at %#x' % root_off,
              'magic=%s%s' % (repr(magic), ' (vendor-mangled)' if vendor else ''))
    if vendor:
        rep.warn('rootfs magic is the vendor mangled form',
                 'only a kernel with the vendor SQUASHFS_MAGIC patch mounts this')

    # squashfs superblock sanity
    if magic == SQUASHFS_MAGIC and root_off + 32 <= len(data):
        inodes, mkfs, block_size, frags, total = struct.unpack(
            '<IIIIH', data[root_off + 4:root_off + 22])
        print('       squashfs: inodes=%d block_size=%d fragments=%d total_bytes=%d' %
              (inodes, block_size, frags, total))
        rep.check(block_size in (65536, 131072, 262144), 'squashfs block size sane')
        remaining = len(data) - root_off
        rep.check(total <= remaining + ERASE,
                  'squashfs total_bytes fits the space left in the image',
                  'total=%d remaining=%d' % (total, remaining))
        if root_off % ERASE:
            rep.warn('rootfs does not start on an erase boundary',
                     'offset %#x, erasesize %#x - overlay/jffs2 geometry must be '
                     'checked on the first boot' % (root_off, ERASE))
    return root_off


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('path')
    ap.add_argument('--tenda', action='store_true',
                    help='input carries the 64-byte Tenda plaintext header')
    ap.add_argument('--stock', action='store_true',
                    help='input is a raw KernelFS partition dump')
    ap.add_argument('--allow-cr6b', action='store_true',
                    help='accept the DIR-842 cr6b signature when inspecting another board\'s dump; this loader cannot boot it')
    ap.add_argument('--expect-start', default='0x81000000',
                    help='start_addr the built image should declare')
    a = ap.parse_args()

    data = open(a.path, 'rb').read()
    rep = Report()
    print('%s  (%d bytes, %#x)' % (a.path, len(data), len(data)))

    off = 0
    if a.tenda:
        h = data[:TENDA_HDR]
        print('  Tenda plaintext header:')
        rep.check(h[:4] == b'\x27\x05\x19\x56', 'magic 27 05 19 56', h[:4].hex())
        plen, tlen = struct.unpack('>II', h[0x0c:0x10] + h[0x28:0x2c])
        rep.check(tlen == len(data), 'total-length field == file size',
                  '%#x vs %#x' % (tlen, len(data)))
        rep.check(plen == len(data) - TENDA_HDR, 'payload-length field',
                  '%#x vs %#x' % (plen, len(data) - TENDA_HDR))
        crc = struct.unpack('>I', h[0x18:0x1c])[0]
        got = zlib.crc32(data[TENDA_HDR:]) & 0xFFFFFFFF
        rep.check(crc == got, 'header crc32 == crc32(payload)',
                  'file %#010x computed %#010x' % (crc, got))
        # The two fields check_fw actually compares (libcommonprod.so@0x2a518:
        # check_fw_type switches on +0x20, check_fw_valid compares +0x24 against
        # the CFG value). tendaupgrade ignores both, which is exactly why they
        # have to be checked HERE: a wrong product id builds green, flashes via
        # tendaupgrade, and is then refused by the webui with errCode=1.
        fam = h[0x20]
        rep.check(fam == 2, 'image family byte +0x20 == 2', '%#x' % fam)
        pid = struct.unpack('>I', h[0x24:0x28])[0]
        rep.check(pid == RX2L_PRO_PRODUCT_ID,
                  'fw.product.id +0x24 == 0x%08x' % RX2L_PRO_PRODUCT_ID,
                  '%#010x' % pid)
        off = TENDA_HDR

    if a.stock:
        # a KernelFS dump: the container is at the very start of mtd3
        rep.check(len(data) == 0xF80000, 'KernelFS partition size', hex(len(data)))

    parse_container(data, off, rep, want_start=int(a.expect_start, 0),
                    allow_cr6b=a.allow_cr6b)

    tail = data[-64:]
    rep.check(b'\x00\xc0\xff\xee' not in tail, 'no D-Link keyed-MD5 trailer',
              'this is not the DIR-842 signing path')

    print('\n%s  (%d failure%s, %d warning%s)' %
          ('OK' if not rep.fails else 'REJECTED',
           len(rep.fails), '' if len(rep.fails) == 1 else 's',
           len(rep.warns), '' if len(rep.warns) == 1 else 's'))
    return 1 if rep.fails else 0


if __name__ == '__main__':
    sys.exit(main())
