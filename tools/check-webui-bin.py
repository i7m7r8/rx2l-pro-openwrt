#!/usr/bin/env python3
"""Gate a candidate image against what the stock webui actually accepts.

Why this file exists
--------------------
The Tenda webui's "Local Upgrade" runs /bin/decry_firm (RSA-4096, unforgeable)
and then DISCARDS its return value: libgo.so upgrade() only requires that
/var/image still exists, that check_fw() likes the format, and then calls the
flasher. So the accepted artifact is a *plaintext* image -- 64-byte Tenda header
+ cr6c container -- and every rule below is a check we can evaluate offline.
See notes/webui-upgrade-contract.md for the addresses this was derived from.

Checks are split by strength:
  * FORMAT rules are derived from the genuine image and tenda-wrap, and are
    re-verified here against any reference image you pass with --reference.
  * BOOT rules come from disassembling OUR mtd1 loader (the code that runs after
    the flash): magic must be cr6c/cs6c and the payload's BE-u16 sum must be 0.
  * The --decry-probe rule reproduces the pass-through property on the real
    binary, which is the single assumption the whole plan rests on.

Usage:
    tools/check-webui-bin.py [--reference <genuine.bin>] [--rootfs DIR]
                             [--probe-dir TMP] image.bin
Exit 0 = every enabled rule passed. Rules that cannot be evaluated are reported
as SKIP, never silently dropped.
"""

import argparse
import os
import shutil
import struct
import subprocess
import sys
import zlib

HDR_MAGIC = b"\x27\x05\x19\x56"
HDR_LEN = 0x40
KERNELFS_MAX = 0xF80000          # mtd3: 0x040000..0xFC0000, the only thing we write
CR6C_MAGICS = (b"cr6c", b"cs6c")  # what the mtd1 loader compares against

# Words we cannot derive: they differ between vendor builds and are not
# checksums of the payload, so they must be copied from a genuine image.
OPAQUE_WORDS = {0x04: "build id/epoch", 0x08: "build id/epoch",
                0x14: "kernel reference (kref)", 0x1c: "hardware/id codes"}

# check_fw_valid() does ntohdr(header+0x24) == strtoul(GetValue("fw.product.id"),16)
# and returns 1 (valid) only on a match. Our CFG partition says product.id=0x00031053
# and the genuine image carries 0x00031053 at +0x24, so this is a checked field,
# not an opaque one.
PRODUCT_ID_OFF = 0x24
RX2LPRO_PRODUCT_ID = 0x00031053
PRODUCT_ID = RX2LPRO_PRODUCT_ID   # set from --product-id in main()


def be32(buf, off):
    return struct.unpack_from(">I", buf, off)[0]


def fail(problems, msg):
    problems.append(msg)
    print("FAIL  " + msg)


def ok(msg):
    print("ok    " + msg)


def check_format(buf, problems):
    """check_fw_type / check_fw_valid / check_fw_crc, as far as we have read them."""
    n = len(buf)
    if n < HDR_LEN + 4:
        fail(problems, "file is smaller than the 64-byte header")
        return None

    if buf[:4] != HDR_MAGIC:
        fail(problems, f"header magic is {buf[:4].hex()}, expected 27 05 19 56")
        return None
    ok(f"header magic {buf[:4].hex()}")

    payload = buf[HDR_LEN:]
    if be32(buf, 0x0c) != len(payload):
        fail(problems, f"payload length at +0x0c is {be32(buf, 0x0c):#x}, "
                       f"file has {len(payload):#x}")
    else:
        ok(f"payload length +0x0c = {len(payload):#x}")

    if be32(buf, 0x28) != n:
        fail(problems, f"total length at +0x28 is {be32(buf, 0x28):#x}, file is {n:#x}")
    else:
        ok(f"total length +0x28 = {n:#x}")

    want = be32(buf, 0x18)
    got = zlib.crc32(payload) & 0xFFFFFFFF
    if want != got:
        fail(problems, f"payload CRC32 at +0x18 is {want:#010x}, computed {got:#010x}")
        return None
    ok(f"payload CRC32 +0x18 = {got:#010x}")

    # check_fw_valid(): header+0x24 must equal the device's fw.product.id.
    pid = be32(buf, PRODUCT_ID_OFF)
    if PRODUCT_ID is not None and pid != PRODUCT_ID:
        fail(problems, f"product id at +{PRODUCT_ID_OFF:#04x} is {pid:#010x} but this "
                       f"router's CFG says {PRODUCT_ID:#010x}; check_fw_valid returns "
                       "1 (valid) only when they match")
    else:
        ok(f"product id +{PRODUCT_ID_OFF:#04x} = {pid:#010x}"
           + ("" if PRODUCT_ID is not None else " (not compared: no --product-id)"))

    # check_fw_type() switches on the family byte at +0x20: 2 and 4 are the
    # shapes genuine images of this generation use (the type-6 layout is a
    # dual-image package, which our single-KernelFS board is not).
    fam = buf[0x20]
    if fam not in (2, 4):
        fail(problems, f"image family byte at +0x20 is {fam}; check_fw_type only "
                       "accepts 2 or 4 for this header magic")
    else:
        ok(f"image family byte +0x20 = {fam}")

    if n > KERNELFS_MAX:
        fail(problems, f"image is {n:#x} bytes; KernelFS (mtd3) is {KERNELFS_MAX:#x} "
                       "and check_mtd_size bounds it by the partition")
    else:
        ok(f"fits KernelFS: {n:#x} of {KERNELFS_MAX:#x} bytes ({100.0*n/KERNELFS_MAX:.1f}%)")
    return payload


def check_boot(payload, problems):
    """The two tests the mtd1 loader really applies (see port/FACTS.md section 3)."""
    sig = payload[:4]
    if sig not in CR6C_MAGICS:
        fail(problems, f"container signature is {sig!r}; the loader accepts only "
                       "cr6c or cs6c and stops with 'No sys signature at %X!'")
        return
    ok(f"container signature {sig.decode()}")

    start, burn, clen = (be32(payload, 4), be32(payload, 8), be32(payload, 12))
    ok(f"start_addr {start:#010x} burn_addr {burn:#010x} len {clen:#x}")
    if clen == 0:
        fail(problems, "container len is 0 -- the loader refuses")
        return
    if 16 + clen + 2 > len(payload):
        fail(problems, f"container len {clen:#x} runs past the end of the payload "
                       f"({len(payload)-16:#x} available)")
        return

    body = payload[16:16 + clen]
    if len(body) % 2:
        fail(problems, "container payload length is odd; the loader sums BE 16-bit words")
        return
    total = 0
    for off in range(0, len(body), 2):
        total += struct.unpack_from(">H", body, off)[0]
    if (total & 0xFFFF) != 0:
        fail(problems, f"loader checksum gate fails: BE-u16 sum over len bytes from "
                       f"+0x10 is {total & 0xFFFF:#06x}, the loader requires 0")
    else:
        ok("loader checksum gate passes (BE-u16 sum == 0)")

    # Our kernel is not mangled: mtdsplit matches the real squashfs magic. Stock
    # vendor images flip hsqs -> nice (and XZ likewise), so both shapes are
    # recognised -- but only one of them is something we may ship.
    tail = payload[16 + clen:16 + clen + 4]
    if tail == b"hsqs":
        ok("squashfs superblock sits at container +0x10+len")
    elif tail == b"nice":
        ok("squashfs superblock sits at container +0x10+len (magic mangled to "
           "'nice' -- this is a STOCK image; our own kernel must ship plain 'hsqs' "
           "because mtdsplit compares against SQUASHFS_MAGIC)")
    else:
        fail(problems, f"expected the squashfs superblock at container +0x10+len, "
                       f"got {tail!r}")

    # The loader runs at LZMA_TEXT_START, so a fat decompressed kernel overwrites it.
    if start == 0x80D00000:
        ok("start_addr matches the stock image's load point")


def check_opaque(buf, reference, problems):
    """Copy-verbatim fields: compare against a genuine plaintext image if given."""
    if reference is None:
        print("SKIP  opaque header words (no --reference given)")
        return
    ref = open(reference, "rb").read()
    if ref[:4] != HDR_MAGIC:
        fail(problems, f"--reference {reference} is not a Tenda plaintext image")
        return
    bad = [f"+0x{o:02x} ({name}): ours {be32(buf, o):#010x} vs genuine "
           f"{be32(ref, o):#010x}" for o, name in OPAQUE_WORDS.items()
           if be32(buf, o) != be32(ref, o)]
    if bad:
        for b in bad:
            fail(problems, "header word we cannot derive differs: " + b)
    else:
        ok("all non-derivable header words match the genuine image")


def check_decry_pass_through(buf, rootfs, probe_dir, problems):
    """Reproduce the property the plan rests on: decry_firm refuses us and leaves
    the bytes alone, so /var/image still holds a whole image afterwards."""
    qemu = shutil.which("qemu-mipsel-static") or shutil.which("qemu-mipsel")
    if not qemu:
        print("SKIP  decry_firm pass-through probe (no qemu-mipsel on PATH)")
        return
    binfmt = os.path.join(rootfs, "bin", "decry_firm")
    if not os.path.exists(binfmt):
        print(f"SKIP  decry_firm pass-through probe ({binfmt} missing)")
        return

    os.makedirs(probe_dir, exist_ok=True)
    work = os.path.join(probe_dir, "image.bin")
    flag = os.path.join(probe_dir, "FAILFLAG")
    with open(work, "wb") as f:
        f.write(buf)
    if os.path.exists(flag):
        os.remove(flag)

    r = subprocess.run([qemu, "-L", rootfs, binfmt, work, flag],
                       capture_output=True, timeout=600)
    after = open(work, "rb").read()
    if r.returncode == 0:
        fail(problems, "decry_firm accepted a plaintext image -- our model of the "
                       "verifier is wrong, re-read it before trusting anything")
        return
    ok(f"decry_firm refuses the plaintext image (rc={r.returncode}): "
       + (r.stdout + r.stderr).decode("utf-8", "replace").strip().splitlines()[-1])
    if after != buf:
        fail(problems, "decry_firm MODIFIED the image on failure -- /var/image would "
                       "not survive, so the webui path is dead")
    else:
        ok("image is byte-identical after the refused decryption")
    if os.path.exists(flag):
        ok("failure flag created (libgo.so ignores it; recorded for the veto question)")
    else:
        print("SKIP  flag file not created")


def main():
    global PRODUCT_ID
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--reference", help="genuine decrypted plaintext image of this model")
    ap.add_argument("--product-id", default=hex(RX2LPRO_PRODUCT_ID),
                    help="this router's fw.product.id, read from the CFG partition")
    ap.add_argument("--rootfs", help="extracted stock rootfs, to run the decry_firm probe")
    ap.add_argument("--probe", action="store_true", help="run the decry_firm probe")
    ap.add_argument("--probe-dir", default="/tmp/webui-check-probe")
    a = ap.parse_args()
    PRODUCT_ID = int(a.product_id, 16) if a.product_id.lower().startswith("0x") \
        else int(a.product_id)

    buf = open(a.image, "rb").read()
    print(f"checking {a.image} ({len(buf):#x} bytes)")
    problems = []

    payload = check_format(buf, problems)
    if payload is not None:
        check_boot(payload, problems)
    check_opaque(buf, a.reference, problems)
    if a.probe:
        check_decry_pass_through(buf, a.rootfs or "", a.probe_dir, problems)

    print()
    if problems:
        print(f"{len(problems)} problem(s): the webui would refuse this file")
        return 1
    print("all rules passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
