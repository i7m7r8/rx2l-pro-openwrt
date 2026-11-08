#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""
port/02-image-container/rx2lpro-image-gates.py
-> scripts/rx2lpro-image-gates.py

Hard, fatal build gates for the Tenda RX2L Pro image recipe.

Why this file exists at all: our stage-1 boot path validates exactly two
things (magic, and a 16-bit additive checksum) and then jumps into a loader
that decompresses the kernel with NO bound check whatsoever. Every other way
this image can be unbootable is therefore invisible to OpenWrt's build system,
and Build/check-size is deliberately non-fatal. So the checks that decide
"boots / does not boot" have to be fatal here.

subcommands
  kernel     the UNCOMPRESSED kernel+DTB must fit between LOADADDR and the
             loader's link address.  Run it inside the KERNEL pipeline, right
             after append-dtb and before lzma -- that intermediate file IS the
             decompressed stream.
  budget     the finished combined image must fit the mtd3 KernelFS window,
             evaluated with padjffs2's erase-block rounding predicted.
  container  re-check the built cr6c container the way the disassembled
             loader does: magic in {cr6c,cs6c}, BE-u16 sum over `len` bytes
             from +0x10 == 0, start_addr sane, rootfs magic at exactly 16+len.
  webui      re-check the finished *-tenda-plaintext.bin the way the stock
             webui does, i.e. check_fw_type()/check_fw_valid()/check_fw_crc()/
             check_mtd_size() as decompiled from libcommon.so 0xa824 and
             libcommonprod.so 0x2a518, then delegate to `container` for what
             happens AFTER the flash.  This is the gate that decides whether
             the upload is accepted at all.
  lzma-size  read the uncompressed size out of the LZMA "alone" header inside
             a loader/kernel blob. Informational: this is the number to put in
             CI logs so the headroom trend is visible before it becomes a
             failure.

Exit codes: 0 ok, 1 gate failed, 2 usage/IO error. Never writes anything.
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
import zlib

UNITS = {"": 1, "b": 1, "k": 1024, "kb": 1024, "kib": 1024,
         "m": 1024 * 1024, "mb": 1024 * 1024, "mib": 1024 * 1024,
         "g": 1024 ** 3, "gb": 1024 ** 3}

# The only two signatures our stage-1 accepts at container +0x00.
# `cr6b` (D-Link DIR-842 R1) is intentionally absent: our loader prints
# "No sys signature at %X!" and drops to the command monitor for it.
BOOT_SIGNATURES = (b"cr6c", b"cs6c")
SQUASHFS_MAGICS = (b"hsqs", b"sqsh")
# squashfs superblock with its magic bytes swapped by the vendor kernel patch;
# a legal value in a stock dump, never in an image we build.
MANGLED_SQUASHFS_MAGIC = b"nice"
HDR = 16

# ---------------------------------------------------------------------------
# The stock webui verifier, as decompiled (see notes/webui-upgrade-contract.md
# and port/08-veto-paths).  Every constant below comes from an instruction,
# not from a guess.
# ---------------------------------------------------------------------------
T_HDR = 0x40
# check_fw_type() memcpy's 0x44 bytes and switches on ntohl(buf+0x00).  On the
# little-endian device ntohl() of the stored bytes recovers the big-endian
# numeric value, so `ntohl(+0x00) == 0x27051956` <=> the four bytes on disk are
# 27 05 19 56 <=> a big-endian u32 read of +0x00 equals 0x27051956.
FW_MAGIC_KERNEL_IMAGE = 0x27051956   # -> fw_cls at +0x20 must be 2
FW_MAGIC_ALT = 0x27051958            # -> fw_cls at +0x20 must be 4
# ...and the third branch compares the RAW word, with no ntohl, to this:
FW_MAGIC_RAW_DUAL = 0x48844884       # -> type 6, the dual-image package
FW_TYPE_KERNEL_IMAGE = 2
# The ONLY rejection on the type-2 path.  Reached when a dual-image helper
# returns 0 AND ntohl(buf+0x40) == 'God1'; the message is
# "Image is dual image,but flash not".  Our container starts with cr6c, i.e.
# 0x63723663, so this never fires -- an invariant, asserted below.
DUAL_IMAGE_TAG = 0x476f6431
DUAL_IMAGE_MSG = "Image is dual image,but flash not"
# check_fw_valid() compares ntohl(+0x24) with
# strtoul(GetValue("fw.product.id"), 16).  CFG (mtd2) on this board says
# product.id=0x00031053 and the genuine image carries 0x00031053 at +0x24.
PRODUCT_ID_RX2LPRO = 0x00031053
# KernelFS (mtd3) is 0x040000..0xFC0000.
KERNELFS_MAX = 0xF80000
# check_fw() in libcommonprod.so 0x2a518 does, BEFORE any of its three
# sub-checks: access("/tmp/UPGRADE_DECRY_FAIL_FLAG", F_OK) -> return -1.
DECRY_FAIL_FLAG = "/tmp/UPGRADE_DECRY_FAIL_FLAG"


def parse_size(text: str) -> int:
    s = str(text).strip().lower()
    if not s:
        raise argparse.ArgumentTypeError("empty size")
    cut = len(s)
    while cut and s[cut - 1].isalpha():
        cut -= 1
    num, suffix = s[:cut], s[cut:]
    if suffix not in UNITS:
        raise argparse.ArgumentTypeError("unsupported size suffix in %r" % text)
    try:
        base = int(num, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("invalid size %r" % text) from exc
    if base <= 0:
        raise argparse.ArgumentTypeError("size must be positive")
    return base * UNITS[suffix]


def human(value: int) -> str:
    return "0x%x (%d bytes, %.3f MiB)" % (value, value, value / 1048576.0)


def read(path: str) -> bytes:
    with open(path, "rb") as fh:
        return fh.read()


def fail(label: str, detail: str) -> int:
    print("GATE FAIL [%s]: %s" % (label, detail), file=sys.stderr)
    return 1


def ok(label: str, detail: str = "") -> None:
    print("GATE OK   [%s]%s" % (label, (": " + detail) if detail else ""))


# --------------------------------------------------------------------------
# kernel gate
# --------------------------------------------------------------------------
def cmd_kernel(args) -> int:
    size = os.path.getsize(args.image)
    budget = args.loader_start - args.loadaddr
    limit = budget - args.reserve
    print("kernel+dtb (uncompressed) %s" % human(size))
    print("  LOADADDR=%#010x loader=%#010x budget=%s reserve=%#x limit=%s"
          % (args.loadaddr, args.loader_start, human(budget), args.reserve,
             human(limit)))
    if size > limit:
        return fail("kernel-size",
                    "decompressed kernel overruns the loader: %s > %s. "
                    "The loader still executes at %#010x while decompressing "
                    "downward from %#010x, so this image boots into an "
                    "overwritten stream. Mitigations, in order of yield: "
                    "strip drivers/config (see REPORT.md), raise "
                    "CVIMG_LOADER_START_ADDR (nothing in mtd1 pins it), or "
                    "split the DTB out of the measured stream."
                    % (human(size), human(limit), args.loader_start,
                       args.loadaddr))
    headroom = limit - size
    pct = 100.0 * size / float(limit)
    if headroom <= args.warn_below:
        print("GATE WARN [kernel-size]: only %s of headroom (%.1f%% used)"
              % (human(headroom), pct), file=sys.stderr)
    else:
        ok("kernel-size", "%s headroom (%.1f%% used)" % (human(headroom), pct))
    return 0


# --------------------------------------------------------------------------
# firmware budget gate
# --------------------------------------------------------------------------
def cmd_budget(args) -> int:
    size = os.path.getsize(args.image)
    limit = args.limit
    predicted = size
    if args.pad:
        # padjffs2 appends a cleanmarker and rounds up to one of
        # 4/8/16/64/128/256k; with BLOCKSIZE := 64k for this board's
        # measured erasesize (0x10000) the effective step is the erase size.
        predicted = ((size + 4 + args.erase_size - 1) // args.erase_size) * args.erase_size
    print("image %s, predicted after pad-rootfs %s, window %s"
          % (human(size), human(predicted), human(limit)))
    if predicted > limit:
        return fail("firmware-budget",
                    "%s exceeds the mtd3 KernelFS window %s by %s. Image left "
                    "on disk for inspection; it must not be flashed."
                    % (human(predicted), human(limit),
                       human(predicted - limit)))
    ok("firmware-budget", "%s spare" % human(limit - predicted))
    return 0


# --------------------------------------------------------------------------
# container gate (the loader's own rules)
# --------------------------------------------------------------------------
def loader_sum(data: bytes, off: int, length: int) -> int:
    """The stage-1 checksum loop: BE u16 words, additive, mod 2**16.

    Verbatim from the disassembly: bytes are read in <=256-byte chunks, each
    word is built as (b0<<8)|b1 -- i.e. big-endian regardless of the MIPS LE
    kernel -- and accumulated with `s0 = (s0 + v1) & 0xffff` over `len` bytes
    starting at container+0x10. A trailing odd byte is taken as b0<<8.
    Pass condition is a sum of exactly 0.
    """
    total = 0
    end = off + length
    if end > len(data):
        return -1
    i = off
    while i + 1 < end:
        total = (total + ((data[i] << 8) | data[i + 1])) & 0xFFFF
        i += 2
    if (end - off) & 1:
        total = (total + (data[end - 1] << 8)) & 0xFFFF
    return total


def cmd_container(args) -> int:
    data = read(args.image)
    off = args.offset
    if len(data) < off + HDR + 2:
        return fail("container", "file shorter than a cvimg header")
    sig = data[off:off + 4]
    # Rule 1: the magic the loader compares against its two literals.
    if sig not in BOOT_SIGNATURES:
        return fail("container",
                    "magic at +%#x is %r; stage-1 only accepts cr6c/cs6c "
                    "and prints 'No sys signature at %%X!' otherwise"
                    % (off, sig))
    start, burn, length = struct.unpack(">III", data[off + 4:off + 16])
    ok("container magic", "%.4s" % sig.decode("latin1"))
    # Rule 2: len must be non-zero (the loader refuses len==0) and the region
    # it names must exist in the file, else we checksum garbage.
    if length == 0:
        return fail("container", "header len == 0 -> loader refuses to boot")
    if off + HDR + length > len(data):
        return fail("container", "len %#x runs past end of file %#x"
                    % (length, len(data)))
    # Rule 3: the zero-sum identity.
    total = loader_sum(data, off + HDR, length)
    if total != 0:
        return fail("container",
                    "BE-u16 sum over %d bytes from +%#x is %#04x, must be 0 "
                    "(loader: 'check failed, ret:%%d, sum:%%x')"
                    % (length, off + HDR, total))
    ok("container checksum", "sum over len=%#x bytes == 0" % length)
    # The copy length stage-1 uses; a 2-byte divergence here means the last
    # payload byte it copies is the checksum, which is what cvimg intends.
    print("       start_addr=%#010x burn_addr=%#010x len=%#x copy=%#x"
          % (start, burn, length, length - 2))
    # Rule 4: start_addr must keep `start_addr | 0x20000000` a valid uncached
    # alias of the cached address, i.e. it must be a KSEG0 0x8xxxxxxx value.
    if args.expect_start is not None and start != args.expect_start:
        return fail("container", "start_addr %#010x != %#010x (the header field "
                                 "is BOTH the jump target and the copy "
                                 "destination; it must equal LZMA_TEXT_START)"
                    % (start, args.expect_start))
    if (start & 0xF0000000) != 0x80000000:
        return fail("container",
                    "start_addr %#010x is not KSEG0; |0x20000000 would not be "
                    "the uncached alias of the same RAM" % start)
    ok("container start_addr", "%#010x -> copy to %#010x"
       % (start, start | 0x20000000))
    if args.expect_burn is not None and burn != args.expect_burn:
        return fail("container", "burn_addr %#x != %#x" % (burn, args.expect_burn))
    # Rootfs placement, the mtdsplit_cvimg / stock-shape contract.
    root_off = off + HDR + length
    magic = data[root_off:root_off + 4]
    if magic == MANGLED_SQUASHFS_MAGIC:
        # The stock kernel carries a patch that reads `hsqs` as `nice`
        # (notes/cr6c-container.md). That is a stock-KERNEL detail, not a
        # container one: our own images must ship plain `hsqs`, or our
        # mtdsplit_cvimg and the squashfs driver both miss the superblock.
        if not args.allow_mangled:
            return fail("container",
                        "rootfs superblock at 16+len (%#x) is the vendor-mangled"
                        " %r: our kernel needs plain hsqs (use --allow-mangled "
                        "to inspect a stock dump)" % (root_off, magic))
        print("GATE WARN [rootfs offset]: %#x holds %s (vendor-mangled, "
              "allowed only for stock dumps)" % (root_off, magic), file=sys.stderr)
        return 0
    if magic not in SQUASHFS_MAGICS:
        return fail("container",
                    "no squashfs magic at 16+len (%#x): got %r. len drives "
                    "both the checksum and the copy length, so the rootfs must "
                    "start immediately after the 2-byte checksum"
                    % (root_off, magic))
    ok("rootfs offset", "%#x (%s)" % (root_off, magic.decode("latin1")))
    return 0


def be32(buf: bytes, off: int) -> int:
    """A big-endian u32, i.e. what the device sees after ntohl()."""
    return struct.unpack_from(">I", buf, off)[0]


def fw_type_of(header: bytes):
    """Reproduce check_fw_type()'s derivation exactly.

    Returns (fw_type, fw_cls, why).  fw_type is None when the function would
    print "unknow fw type." / "get fw type is fail." and return -1.
    The type is taken FROM THE HEADER -- no CGI parameter can change it.
    """
    magic = be32(header, 0x00)
    raw = struct.unpack_from("<I", header, 0x00)[0]
    cls = header[0x20]
    if magic == FW_MAGIC_KERNEL_IMAGE:
        if cls != FW_TYPE_KERNEL_IMAGE:
            return (None, cls,
                    "ntohl(+0x00)==0x27051956 selects FW_TYPE_KERNEL_IMAGE, so "
                    "the fw_cls byte at +0x20 must be %d; loader log line would be "
                    "'pimg_header->fw_cls:%%x,FW_TYPE_KERNEL_IMAGE:%%x.' with "
                    "%d vs %d" % (FW_TYPE_KERNEL_IMAGE, cls,
                                  FW_TYPE_KERNEL_IMAGE))
        return (FW_TYPE_KERNEL_IMAGE, cls, "ntohl(+0x00)=0x27051956, fw_cls=2")
    if magic == FW_MAGIC_ALT:
        if cls != 4:
            return (None, cls,
                    "ntohl(+0x00)==0x27051958 requires fw_cls 4 at +0x20, got %d"
                    % cls)
        return (4, cls, "ntohl(+0x00)=0x27051958, fw_cls=4")
    if raw == FW_MAGIC_RAW_DUAL:
        return (6, cls, "raw +0x00 == 0x48844884 -> type 6 (dual-image package)")
    return (None, cls, "unknow fw type.: ntohl(+0x00)=%#010x and raw +0x00=%#010x "
                       "match none of 0x27051956 / 0x27051958 / 0x48844884"
                       % (magic, raw))


def cmd_webui(args) -> int:
    """The stock webui's own acceptance rules, applied to our finished file.

    Chain, with the addresses this mirrors:
      libgo.so upgrade()  0x4439c mv /tmp/<upload> /var/image
                          0x443ec decry_firm /var/image /tmp/UPGRADE_DECRY_FAIL_FLAG
                          0x44414 stat64(/var/image)   <- only "does it still exist"
                          0x445a4 systool_upgradefile_handle -> check_fw()
                          0x44708 rc==0 -> systool_call_writeImage_delay_11ac(fw_type)
      check_fw  (libcommonprod.so 0x2a518) for type 2, in order:
        access("/tmp/UPGRADE_DECRY_FAIL_FLAG", F_OK)  present -> -1
        check_fw_valid(2, path)  fail -> -1
        check_fw_crc(2, buf, len) fail -> -2
        check_mtd_size(2, path)  fail -> -3
    """
    data = read(args.image)
    n = len(data)
    label = args.label

    # ---- check_fw_type: size, then the type derived from the header ----
    if n < T_HDR + 4:
        return fail(label + "/fw-type", "file is smaller than the 64-byte header")
    fw_type, cls, why = fw_type_of(data[:0x44])
    if fw_type is None:
        return fail(label + "/fw-type", why)
    if fw_type == 6:
        return fail(label + "/fw-type",
                    "type 6 is the dual-image package layout; this board has one "
                    "KernelFS (no KernelFS1/2), so check_fw_valid() would go on to "
                    "demand the second header and 'only boot image' / "
                    "'second image error'")
    if fw_type != args.expect_cls:
        return fail(label + "/fw-type",
                    "derived fw_type %d != the recipe's %d" % (fw_type, args.expect_cls))
    ok(label + "/fw-type", why)

    # ---- THE dual-image trap: +0x40 must not be 'God1' ----
    tag = be32(data, T_HDR)
    if tag == DUAL_IMAGE_TAG:
        return fail(label + "/dual-image",
                    "the four bytes at +0x40 are 'God1' (%#010x): the dual-image "
                    "helper returns 0 and check_fw_type() prints '%s' and refuses. "
                    "This is the ONLY rejection on the type-2 path."
                    % (DUAL_IMAGE_TAG, DUAL_IMAGE_MSG))
    sig = data[T_HDR:T_HDR + 4]
    if sig not in BOOT_SIGNATURES:
        return fail(label + "/dual-image",
                    "+0x40 is %.4r, not a cr6c/cs6c container: the type-2 branch "
                    "would pass the God1 test and then the stage-1 loader would "
                    "print 'No sys signature at %%X!'" % sig)
    ok(label + "/dual-image",
       "+0x40 = %.4s -> ntohl(+0x40)=%#010x != %#010x ('God1'): type %d is "
       "returned unconditionally" % (sig.decode("latin1"), tag, DUAL_IMAGE_TAG,
                                     fw_type))

    # ---- check_fw_crc: +0x18 must be crc32(payload) ----
    payload = data[T_HDR:]
    want = be32(data, 0x18)
    got = zlib.crc32(payload) & 0xFFFFFFFF
    if want != got:
        return fail(label + "/fw-crc",
                    "CRC32 at +0x18 is %#010x, crc32(payload) is %#010x "
                    "(check_fw_crc fail -> -2)" % (want, got))
    ok(label + "/fw-crc", "+0x18 = %#010x over %#x payload bytes" % (got, len(payload)))

    # ---- check_fw_valid: +0x24 must equal this product's fw.product.id ----
    pid = be32(data, 0x24)
    if pid != args.product_id:
        return fail(label + "/product-id",
                    "ntohl(+0x24)=%#010x but check_fw_valid() compares it with "
                    "strtoul(GetValue(\"fw.product.id\"),16)=%#010x read from the "
                    "CFG partition; a mismatch is -1 and the webui refuses"
                    % (pid, args.product_id))
    ok(label + "/product-id", "+0x24 = %#010x" % pid)

    # ---- the two length fields the vendor header carries ----
    plen, tlen = be32(data, 0x0c), be32(data, 0x28)
    if plen != len(payload):
        return fail(label + "/lengths",
                    "payload length at +0x0c is %#x, file has %#x payload bytes"
                    % (plen, len(payload)))
    if tlen != n:
        return fail(label + "/lengths",
                    "total length at +0x28 is %#x but the file is %#x" % (tlen, n))
    ok(label + "/lengths", "+0x0c = %#x, +0x28 = %#x == file size" % (plen, tlen))

    # ---- check_mtd_size: bound by KernelFS.  Which of the two quantities the
    # vendor compares is still unverified, so bound BOTH, conservatively.
    if args.kernelfs_max is not None:
        limit = args.kernelfs_max
        if n > limit:
            return fail(label + "/mtd-size",
                        "total image %#x exceeds the KernelFS window %#x; "
                        "check_mtd_size fail -> -3 and it cannot fit mtd3 anyway"
                        % (n, limit))
        if plen > limit:
            return fail(label + "/mtd-size",
                        "payload %#x exceeds the KernelFS window %#x" % (plen, limit))
        ok(label + "/mtd-size", "payload %#x, total %#x of %#x (%.1f%%)"
           % (plen, n, limit, 100.0 * n / limit))

    # ---- the flag-file veto: real, and not something a build can satisfy ----
    print("GATE NOTE [%s/decry-flag]: check_fw() starts with "
          "access(\"%s\", F_OK) and returns -1 if it exists ('---image decrypted "
          "failed---'). decry_firm creates it whenever the RSA step fails, which "
          "it always does for a plaintext image, and libgo.so discards decry_firm's "
          "return value -- so this file decides the webui path and no build-time "
          "gate can influence it. Owner: port/08-veto-paths." % (label, DECRY_FAIL_FLAG),
          file=sys.stderr)

    # ---- what runs AFTER the flash: the container rules, at +0x40 ----
    sub = argparse.Namespace(image=args.image, offset=T_HDR,
                             expect_start=args.expect_start,
                             expect_burn=args.expect_burn,
                             allow_mangled=args.allow_mangled,
                             label=label + "/boot")
    return cmd_container(sub)


# --------------------------------------------------------------------------
# lzma-size (informational)
# --------------------------------------------------------------------------
def lzma_alone_at(data: bytes, i: int):
    """Decode an LZMA 'alone' header at i, or return None if implausible.

    Plausibility, for a MIPS kernel stream produced by our own pipeline: the
    props byte must decode the way LzmaDecodeProperties() does (prop0 < 9*5*5,
    then pb/lp/lc), the dictionary must be a power of two within
    64 KiB..64 MiB, and the recorded uncompressed size within 512 KiB..64 MiB.
    Those ranges are a *heuristic* to kill false positives in a byte scan --
    the decoder itself only checks prop0 < 225. The vendor's real header is
    5d 00 00 80 00 + size, i.e. lc=3 lp=0 pb=1 dict=8 MiB, measured at
    container+0x2818 of mtd3 (uncompressed 13384292).
    """
    if i + 13 > len(data):
        return None
    props = data[i]
    if props >= 9 * 5 * 5:
        return None
    rem = props
    pb = 0
    while rem >= 9 * 5:
        rem -= 9 * 5
        pb += 1
    lp = 0
    while rem >= 9:
        rem -= 9
        lp += 1
    lc = rem
    if lc > 4 or lp > 4 or pb > 4:
        return None
    (dict_size,) = struct.unpack_from("<I", data, i + 1)
    if dict_size < 0x10000 or dict_size > 0x4000000 or dict_size & (dict_size - 1):
        return None
    (size,) = struct.unpack_from("<Q", data, i + 5)
    if size == 0xFFFFFFFFFFFFFFFF or size < 0x80000 or size > 0x4000000:
        return None
    return props, dict_size, size


def cmd_lzma_size(args) -> int:
    """Report the uncompressed size recorded in LZMA-alone header(s).

    This is the number the 16 MiB budget is actually about, and it is readable
    straight out of the built container -- no symbol tables, no rebuild.
    """
    data = read(args.image)
    if args.at is not None:
        hit = lzma_alone_at(data, args.at)
        if hit is None:
            print("lzma-size: nothing plausible at %#x in %s" % (args.at, args.image))
            return 1
        props, dict_size, size = hit
        print("lzma-size @%#x: props=%#02x dict=%#x uncompressed=%s -> %s"
              % (args.at, props, dict_size, human(size),
                 "FITS" if size < args.limit else "TOO BIG"))
        return 0 if size < args.limit else 1
    found = []
    for i in range(0, len(data) - 13):
        hit = lzma_alone_at(data, i)
        if hit:
            found.append((i,) + hit)
    if not found:
        print("lzma-size: no plausible LZMA-alone header in %s" % args.image)
        return 0
    for off, props, dict_size, size in found[:args.max]:
        verdict = "FITS" if size < args.limit else "TOO BIG"
        print("lzma-size @%#x: props=%#02x dict=%#x uncompressed=%s -> %s"
              % (off, props, dict_size, human(size), verdict))
    return 0


# --------------------------------------------------------------------------
def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description="fatal image gates for the Tenda RX2L Pro (cr6c) recipe")
    sub = ap.add_subparsers(dest="cmd", required=True)

    k = sub.add_parser("kernel")
    k.add_argument("--image", required=True)
    k.add_argument("--loadaddr", type=lambda v: int(v, 0), default=0x80000000)
    k.add_argument("--loader-start", type=lambda v: int(v, 0), required=True)
    k.add_argument("--reserve", type=parse_size, default="64k")
    k.add_argument("--warn-below", type=parse_size, default="1M",
                   help="warn when headroom drops under this")
    k.add_argument("--label", default="rx2lpro")
    k.set_defaults(func=cmd_kernel)

    b = sub.add_parser("budget")
    b.add_argument("--image", required=True)
    b.add_argument("--limit", type=parse_size, required=True)
    b.add_argument("--erase-size", type=parse_size, default="64k")
    b.add_argument("--pad", action="store_true",
                   help="predict padjffs2's erase-block rounding")
    b.add_argument("--label", default="rx2lpro")
    b.set_defaults(func=cmd_budget)

    c = sub.add_parser("container")
    c.add_argument("image")
    c.add_argument("--offset", type=lambda v: int(v, 0), default=0)
    c.add_argument("--expect-start", type=lambda v: int(v, 0), default=None)
    c.add_argument("--expect-burn", type=lambda v: int(v, 0), default=None)
    c.add_argument("--allow-mangled", action="store_true",
                   help="accept a vendor-mangled squashfs magic (stock dumps only)")
    c.set_defaults(func=cmd_container)

    w = sub.add_parser("webui")
    w.add_argument("image")
    w.add_argument("--label", default="rx2lpro")
    w.add_argument("--expect-cls", type=int, default=2,
                   help="check_fw_type() must report this (2 = KernelFS)")
    w.add_argument("--product-id", type=lambda v: int(v, 0), default=0x00031053)
    w.add_argument("--kernelfs-max", type=parse_size, default="15872k")
    # forwarded to the container check that runs on the payload at +0x40
    w.add_argument("--expect-start", type=lambda v: int(v, 0), default=None)
    w.add_argument("--expect-burn", type=lambda v: int(v, 0), default=None)
    w.add_argument("--allow-mangled", action="store_true",
                   help="accept a vendor-mangled squashfs magic (stock dumps only)")
    w.set_defaults(func=cmd_webui)

    l = sub.add_parser("lzma-size")
    l.add_argument("image")
    l.add_argument("--limit", type=parse_size, default="16M")
    l.add_argument("--max", type=int, default=8)
    l.add_argument("--at", type=lambda v: int(v, 0), default=None,
                   help="read the header at this exact offset and gate on it")
    l.set_defaults(func=cmd_lzma_size)

    args = ap.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
