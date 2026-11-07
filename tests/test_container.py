#!/usr/bin/env python3
"""Container self-test for the Tenda RX2L Pro image pipeline.

Extended replacement for tests/test_container.py. The original proved that
tools/check-cr6c.py and files/tools/firmware-utils/src/tenda-wrap.c agree with
the documented container spec. This adds the part that actually decides
whether the router boots: the checks OUR stage-1 loader performs, taken from
the disassembly of this board's own mtd1 (/tmp/work4/FINDINGS.md sections 2
and 3), which is authoritative for the boot path:

  B1  the 4 bytes at container+0x00 must equal "cr6c" or "cs6c", otherwise
      U-Boot prints "No sys signature at %X!" and drops to the command
      monitor -- "cr6b" (the D-Link DIR-842 signature our old reference
      recipe used) is NOT bootable here.
  B2  the additive sum of BIG-ENDIAN 16-bit words over `len` bytes starting at
      container+0x10 must be exactly 0 mod 2**16, otherwise
      "check failed, ret:%d, sum:%x" and no boot. Verified numerically on the
      device's own KernelFS dump (case 8 below).
  B3  len (header +0x0c, BE) drives BOTH the checksum length and the copy
      length (`len - 2` bytes copied to start_addr), so the squashfs superblock
      must sit at exactly 16 + len.
  B4  start_addr (+0x04, BE) is both the jump target and the copy destination
      (`start_addr | 0x20000000`), so it must be a KSEG0 0x8xxxxxxx value and
      must equal the lzma-loader's link address.
  B5  burn_addr (+0x08) is read, byteswapped and then never used -- we still
      write the truthful 0x40000 (mtd3 flash offset) for the vendor updater.
  B6  there is NO RSA / TDE01 / KM / squashfs-signature gate on the boot path,
      so nothing here may depend on one.

It also exercises the two host tools as builders rather than as data:
cvimg.c is compiled and run with exactly the flags the image recipe passes, and
its output must satisfy B1-B5; and scripts/rx2lpro-image-gates.py (staged as
port/02-image-container/rx2lpro-image-gates.py) is run as the fatal build gate.

    python3 tests/test_container.py       # needs gcc for the tool builds
Run from the repo root. No vendor data is required; case 8 runs only if a
KernelFS dump happens to be on disk.
"""
import os
import random
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))


def _find_root():
    """Repo root, found by marker file rather than by directory depth.

    This file is staged at port/02-image-container/ and lands at tests/, so its
    own depth is not information; tools/check-cr6c.py is.
    """
    d = HERE
    for _ in range(6):
        if os.path.exists(os.path.join(d, 'tools', 'check-cr6c.py')):
            return d
        parent = os.path.dirname(d)
        if parent == d:
            break
        d = parent
    # not found: fall back to the integrated layout (tests/ next to tools/)
    return os.path.dirname(HERE)


ROOT = _find_root()
CHECKER = os.path.join(ROOT, 'tools', 'check-cr6c.py')
CVIMG_SRC = os.path.join(ROOT, 'files', 'tools', 'firmware-utils', 'src', 'cvimg.c')
WRAP_SRC = os.path.join(ROOT, 'files', 'tools', 'firmware-utils', 'src', 'tenda-wrap.c')
# staged location first, integrated location second
GATES_CANDIDATES = [
    os.path.join(ROOT, 'scripts', 'rx2lpro-image-gates.py'),
    os.path.join(ROOT, 'port', '02-image-container', 'rx2lpro-image-gates.py'),
]
# the vendor KernelFS dump, if whoever runs this kept it outside the repo
STOCK_DUMP_CANDIDATES = ['/tmp/work4/mtd3_KernelFS.bin', '/tmp/mtd3_KernelFS.bin']

# These four mirror the image Makefile device block. If they drift apart from
# target/linux/*/image/Makefile, cases 6/7/9 are what catch it.
START = 0x81000000          # CVIMG_LOADER_START_ADDR == LZMA_TEXT_START
BURN = 0x40000              # CVIMG_KERNEL_BURN_ADDR == mtd3 flash offset
KREF = 0x808d45c0           # CVIMG_TENDA_KREF, header +0x14
LOADADDR = 0x80000000       # where the loader decompresses to
HDR = 16
TENDA_HDR = 0x40
fails = []
gaps = []
skips = []


# --------------------------------------------------------------------------
# the two rules the loader implements, written the way the disassembly does
# --------------------------------------------------------------------------
def loader_gate(data, off=0):
    """(ok, why) for the stage-1 validation path: B1 + B2 (+ len sanity)."""
    if len(data) < off + HDR + 2:
        return False, 'file shorter than a cvimg header'
    magic = data[off:off + 4]
    if magic not in (b'cr6c', b'cs6c'):
        return False, ("No sys signature at %X: magic %r is not cr6c/cs6c"
                      % (off, magic))
    start, burn, ln = struct.unpack('>III', data[off + 4:off + 16])
    if ln == 0:
        return False, 'header len == 0 -> 10be4 refuses'
    if ln > len(data) - off - HDR:
        return False, 'header len %#x runs past the end of the container' % ln
    total = 0
    pos = off + HDR
    end = pos + ln
    # the loader reads in <=256-byte chunks and folds (b0<<8)|b1, mod 2**16
    while pos + 1 < end:
        total = (total + ((data[pos] << 8) | data[pos + 1])) & 0xFFFF
        pos += 2
    if (end - (off + HDR)) & 1:
        total = (total + (data[end - 1] << 8)) & 0xFFFF
    if total != 0:
        return False, 'check failed, sum:%04x (must be 0)' % total
    return True, 'magic ok, BE-u16 sum over %#x bytes == 0' % ln


def cvimg_checksum(buf):
    """cvimg.c calculate_checksum(): 16-bit big-endian word sum, htons(~sum + 1)."""
    s = 0
    for i in range(0, len(buf) - 1, 2):
        s = (s + ((buf[i] << 8) | buf[i + 1])) & 0xFFFF
    if len(buf) % 2:
        s = (s + (buf[-1] << 8)) & 0xFFFF
    return struct.pack('>H', (~s + 1) & 0xFFFF)


def squashfs(total_bytes=4096):
    """A superblock shaped like squashfs 4.0 -- enough for the checker's reads."""
    sb = bytearray(96)
    sb[0:4] = b'hsqs'
    struct.pack_into('<I', sb, 4, 512)          # inodes
    struct.pack_into('<I', sb, 8, 0x60000000)   # mkfs_time
    struct.pack_into('<I', sb, 12, 65536)       # block_size
    struct.pack_into('<I', sb, 16, 8)           # fragments
    struct.pack_into('<Q', sb, 20, total_bytes)
    struct.pack_into('<Q', sb, 28, 0)           # root inode ref
    struct.pack_into('<I', sb, 36, 16)          # block_log
    struct.pack_into('<H', sb, 40, 0)           # flags
    struct.pack_into('<H', sb, 42, 1)           # no_ids
    struct.pack_into('<H', sb, 44, 4)           # s_major
    struct.pack_into('<H', sb, 46, 0)           # s_minor
    return bytes(sb) + bytes(bytearray(os.urandom(4096 - 96)))


def synth(payload_len, pad_to_align=False, sig=b'cr6c', start=START, burn=BURN):
    """[hdr][payload][2-byte checksum][(optional padding)][squashfs]."""
    rnd = random.Random(payload_len)
    payload = bytes(bytearray(rnd.getrandbits(8) for _ in range(payload_len)))
    len_field = len(payload) + 2
    hdr = sig + struct.pack('>III', start, burn, len_field)
    body = hdr + payload + cvimg_checksum(payload)
    if pad_to_align:
        while len(body) % 4:
            body += b'\x00'
    return body + squashfs(), payload, len_field


def run_checker(path, tenda=False, extra=()):
    cmd = [sys.executable, CHECKER]
    if tenda:
        cmd.append('--tenda')
    cmd += list(extra)
    cmd += ['--expect-start', hex(START), path]
    p = subprocess.run(cmd, capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def run_gates(*args):
    """Run scripts/rx2lpro-image-gates.py; returns (rc, out, path) or None."""
    for script in GATES_CANDIDATES:
        if os.path.exists(script):
            p = subprocess.run([sys.executable, script] + list(args),
                               capture_output=True, text=True)
            return p.returncode, p.stdout + p.stderr, script
    return None


def check(ok, label, detail=''):
    print('  [%s] %s%s' % ('PASS' if ok else 'FAIL', label,
                           (' - ' + detail) if detail else ''))
    if not ok:
        fails.append(label)
    return ok


def note_gap(label, detail=''):
    print('  [GAP] %s%s' % (label, (' - ' + detail) if detail else ''))
    gaps.append(label)


def note_skip(label, detail=''):
    print('  [SKIP] %s%s' % (label, (' - ' + detail) if detail else ''))
    skips.append(label)


# --------------------------------------------------------------------------
def build_tools(tmp):
    """Compile the two host tools the recipe uses. Returns (cvimg, wrap) or None."""
    cvimg = os.path.join(tmp, 'cvimg')
    wrap = os.path.join(tmp, 'tenda-wrap')
    if not os.path.exists(CVIMG_SRC):
        note_skip('cvimg build', 'no %s' % CVIMG_SRC)
        return None, None
    r = subprocess.run(['gcc', '-Wall', '-O2', '-o', cvimg, CVIMG_SRC],
                       capture_output=True, text=True)
    if not check(r.returncode == 0, 'cvimg compiles clean',
                 r.stderr.strip()[-200:]):
        return None, None
    r = subprocess.run(['gcc', '-Wall', '-Wextra', '-Werror', '-O2',
                        '-o', wrap, WRAP_SRC], capture_output=True, text=True)
    if not check(r.returncode == 0, 'tenda-wrap compiles clean',
                 r.stderr.strip()[-200:]):
        return None, None
    return cvimg, wrap


def case_basic_containers(tmp):
    print('1. plain container, payload 0x1200 (16+len is 4-aligned)')
    data, payload, len_field = synth(0x1200, pad_to_align=False)
    plain = os.path.join(tmp, 'synthetic-cr6c.bin')
    open(plain, 'wb').write(data)
    rc, out = run_checker(plain)
    check(rc == 0, 'checker accepts it', 'rc=%d' % rc)
    check('exact offset' in out, 'took the exact 16+len candidate')
    # B1/B2 must agree with the checker on the same bytes
    ok, why = loader_gate(data)
    check(ok, 'loader gate accepts it', why)

    print('2. container padded so only the ALIGN(16+len,4) candidate hits')
    data2, _, _ = synth(0x1200, pad_to_align=True)
    aligned_path = os.path.join(tmp, 'aligned.bin')
    open(aligned_path, 'wb').write(data2)
    rc, out = run_checker(aligned_path)
    check(rc == 0, 'checker accepts the padded layout', 'rc=%d' % rc)
    check('exact offset' not in out, 'padded layout resolved on the aligned candidate')


def case_cvimg_writer(cvimg, tmp):
    print('6. the real cvimg writer, run with the recipe\'s own flags (B1-B5)')
    if cvimg is None:
        note_skip('cvimg writer cases', 'host tool not built')
        return
    for n in (0x1200, 0x1201, 0x1203, 0x3c0801):
        payload = os.path.join(tmp, 'payload-%x' % n)
        out = os.path.join(tmp, 'container-%x' % n)
        rnd = random.Random(n)
        open(payload, 'wb').write(
            bytes(bytearray(rnd.getrandbits(8) for _ in range(n))))
        r = subprocess.run([cvimg, '-i', payload, '-o', out,
                            '-e', hex(START), '-b', hex(BURN), '-s', 'cr6c'],
                           capture_output=True, text=True)
        if not check(r.returncode == 0, 'cvimg -s cr6c accepts payload %#x' % n,
                     (r.stderr or r.stdout).strip()[-160:]):
            continue
        data = open(out, 'rb').read()
        start, burn, ln = struct.unpack('>III', data[4:16])
        payload_out = ln - 2
        # B3: len = payload (rounded to 2) + 2, and the file is exactly
        # 16 + payload_out + 2 long, so nothing sits between checksum and rootfs
        check(payload_out == ((n + 1) & ~1), 'len == payload+2 (2-aligned)',
              'payload %#x -> len %#x' % (n, ln))
        check(len(data) == HDR + payload_out + 2, 'file is hdr+payload+cksum',
              hex(len(data)))
        check(start == START and burn == BURN, 'B4/B5 start_addr and burn_addr',
              '%#x/%#x' % (start, burn))
        ok, why = loader_gate(data)
        check(ok, 'B1+B2 loader gate on writer output', why)
        # B3 rootfs placement: append the squashfs exactly where the kernel
        # will find it, then assert the offset is what 16+len says.
        with open(out, 'ab') as fh:
            fh.write(squashfs())
        data = open(out, 'rb').read()
        root_off = HDR + ln
        check(data[root_off:root_off + 4] == b'hsqs',
              'B3 squashfs superblock at exactly 16+len = %#x' % root_off,
              'offset %% 4 == %d' % (root_off % 4))
        if root_off % 4:
            aligned = (root_off + 3) & ~3
            check(data[aligned:aligned + 4] != b'hsqs',
                  'the ALIGN(16+len,4) candidate misses here (why the parser '
                  'was fixed)', 'exact %#x, aligned %#x' % (root_off, aligned))
        ok, why = loader_gate(data)
        check(ok, 'loader gate still passes after the rootfs append', why)
        # the fatal build gate must agree with the loader on the same file
        res = run_gates('container', out, '--expect-start', hex(START),
                        '--expect-burn', hex(BURN))
        if res is None:
            note_skip('gates container case', 'scripts/rx2lpro-image-gates.py absent')
        else:
            rc, gout, _ = res
            check(rc == 0, 'rx2lpro-image-gates.py container accepts it',
                  gout.strip().splitlines()[-1][:160])


def case_loader_negatives(tmp):
    print('7. the loader refuses what our recipe must never emit')
    good, _, _ = synth(0x2000)

    # B1: cr6b is the DIR-842 signature. Our stage-1 has only two literals,
    # "cs6c" and "cr6c", so this must be refused outright.
    bad, _, _ = synth(0x2000, sig=b'cr6b')
    ok, why = loader_gate(bad)
    check(not ok, 'cr6b is refused (no boot, monitor + fail bit)', why)
    path = os.path.join(tmp, 'cr6b.bin')
    open(path, 'wb').write(bad + squashfs())
    res = run_gates('container', path)
    if res:
        check(res[0] != 0, 'gates also reject cr6b', res[1].strip()[:160])
    rc, out = run_checker(path)
    if rc == 0:
        note_gap('tools/check-cr6c.py still accepts cr6b',
                 'its sig tuple is (cr6c, cr6b, cs6c); our loader takes only '
                 'cr6c/cs6c -- tighten it (see REPORT.md)')

    # B1 positive control: cs6c boots too (kernel-only signature)
    kref, _, _ = synth(0x2000, sig=b'cs6c')
    ok, why = loader_gate(kref)
    check(ok, 'cs6c is accepted', why)

    # B2: one flipped payload byte must break the zero-sum
    flip = bytearray(good)
    flip[0x400] ^= 0xFF
    ok, why = loader_gate(bytes(flip))
    check(not ok, 'flipped payload byte breaks the zero-sum', why)

    # B2: the checksum word itself, at 16+payload, i.e. the last two bytes of
    # the len window (16 + len). Squashfs bytes beyond it are not covered.
    ln = struct.unpack('>I', good[12:16])[0]
    ck_off = HDR + ln - 2
    wrongsum = bytearray(good)
    wrongsum[ck_off] ^= 0x01
    ok, why = loader_gate(bytes(wrongsum))
    check(not ok, 'flipped checksum byte refused', why)
    tail = bytearray(good)
    tail[len(good) - 1] ^= 0xFF
    ok, why = loader_gate(bytes(tail))
    check(ok, 'B2 window ends at 16+len: bytes past it are unchecked', why)

    # B3: len overrunning the file
    hdr = b'cr6c' + struct.pack('>III', START, BURN, 0x900000)
    ok, why = loader_gate(hdr + bytes(4096))
    check(not ok, 'len past end of file refused', why)

    # len == 0 refuses
    z = bytearray(synth(0x200)[0])
    struct.pack_into('>I', z, 12, 0)
    ok, why = loader_gate(bytes(z))
    check(not ok, 'len == 0 refused', why)

    # B4: start_addr must stay KSEG0 (|0x20000000 has to alias the same RAM).
    # Note the header words themselves are OUTSIDE the checksum window (which
    # starts at +0x10), so B2 cannot catch a wrong start_addr -- that is why
    # the gates take --expect-start and compare it to LZMA_TEXT_START.
    for bad_start in (0xa1000000, 0x00000000, 0xb0040000):
        d = bytearray(synth(0x2000)[0])
        struct.pack_into('>I', d, 4, bad_start)
        ok, why = loader_gate(bytes(d))
        check(ok, 'B2 stays closed with a tampered start_addr %#x '
                  '(header is unchecked)' % bad_start, why)
        check(not ((bad_start & 0xF0000000) == 0x80000000),
              'B4 start_addr %#x would not be a KSEG0 alias' % bad_start)
    # the recipe's two addresses must not be allowed to disagree: a container
    # that names the vendor's 0x80d00000 is loader-valid but would jump into
    # our loader as if it were linked there.
    d = bytearray(synth(0x2000)[0])
    struct.pack_into('>I', d, 4, 0x80d00000)   # the vendor's own value
    ok, why = loader_gate(bytes(d))
    check(ok, 'B1+B2 alone cannot see a wrong start_addr', why)
    path = os.path.join(tmp, 'vendor-start.bin')
    open(path, 'wb').write(bytes(d) + squashfs())
    res = run_gates('container', path, '--expect-start', hex(START))
    if res:
        check(res[0] != 0,
              'gates refuse a container whose start_addr != LZMA_TEXT_START',
              res[1].strip().splitlines()[-1][:160])


def case_corruption(tmp, data):
    print('4. one flipped byte must be caught by the shipped checker')
    bad = bytearray(data)
    bad[0x40 + 500] ^= 0xFF
    bad_path = os.path.join(tmp, 'corrupt.bin')
    open(bad_path, 'wb').write(bytes(bad))
    rc, out = run_checker(bad_path)
    check(rc != 0, 'corrupted payload is rejected')
    check('checksum' in out, 'the rejection names the checksum')


def case_formula():
    print('5. cvimg checksum matches the vendor formula on a known vector')
    vec = bytes(bytearray(range(256))) * 4
    alt = sum(struct.unpack('>H', vec[i:i + 2])[0] for i in range(0, len(vec), 2)) & 0xFFFF
    check(cvimg_checksum(vec) == struct.pack('>H', (~alt + 1) & 0xFFFF),
          'checksum implementation agrees with an independent form')
    # and the identity the loader depends on: sum(payload + cksum) == 0
    total = sum(struct.unpack('>H', vec[i:i + 2])[0]
                for i in range(0, len(vec), 2))
    ck = cvimg_checksum(vec)
    total = (total + struct.unpack('>H', ck)[0]) & 0xFFFF
    check(total == 0, 'B2 identity closes on itself', 'sum=%#x' % total)


def case_tenda(wrap, container_path, tmp):
    print('3. tenda-wrap reproduces the vendor header shape (incl. +0x18 crc32)')
    if wrap is None:
        note_skip('tenda-wrap cases', 'host tool not built')
        return
    wrapped = os.path.join(tmp, 'synthetic-tenda.bin')
    data = open(container_path, 'rb').read()
    r = subprocess.run([wrap, '-i', container_path, '-o', wrapped,
                        '-c', 'cr6c', '-k', hex(KREF)],
                       capture_output=True, text=True)
    if not check(r.returncode == 0, 'tenda-wrap runs',
                 (r.stderr or r.stdout).strip()[-200:]):
        return
    w = open(wrapped, 'rb').read()
    head = w[:TENDA_HDR]
    check(head[:4] == b'\x27\x05\x19\x56', 'magic 27 05 19 56')
    got_plen, got_kref, got_crc, got_tlen = struct.unpack(
        '>IIII', head[0x0c:0x10] + head[0x14:0x18] + head[0x18:0x1c] + head[0x28:0x2c])
    check(got_plen == len(data), 'payload-length == container size',
          '%#x vs %#x' % (got_plen, len(data)))
    check(got_tlen == len(w), 'total-length == wrapped size', '%#x/%#x' % (got_tlen, len(w)))
    check(got_crc == zlib.crc32(data) & 0xFFFFFFFF, 'crc32 at +0x18 == crc32(payload)',
          '%#010x' % got_crc)
    check(got_kref == KREF, 'kref lands at +0x14')
    check(w[0x10:0x14] == b'\x80\x00\x00\x00', '+0x10 constant 0x80000000')
    check(w[0x1c:0x24] == bytes.fromhex('0505020302000000'), '+0x1c constants')
    check(w[0x24:0x28] == bytes.fromhex('00031053'), '+0x24 constant')
    check(w[0x2c:0x40] == b'\x00' * 0x14, 'tail zeros')
    check(w[TENDA_HDR:] == data, 'container passed through unchanged')
    rc, out = run_checker(wrapped, tenda=True)
    check(rc == 0, 'checker accepts the wrapped file', 'rc=%d' % rc)
    # B6: the wrapper is transport, not a boot gate -- and the loader must
    # validate the container that sits BEHIND it, at +0x40.
    ok, why = loader_gate(w, TENDA_HDR)
    check(ok, 'B1+B2 loader gate on the wrapped file at +0x40', why)
    check(not loader_gate(w, 0)[0],
          'B6 the 0x40-byte Tenda header alone is not a bootable container',
          loader_gate(w, 0)[1][:120])
    res = run_gates('container', wrapped, '--offset', hex(TENDA_HDR),
                    '--expect-start', hex(START))
    if res:
        check(res[0] == 0, 'gates validate at --offset 0x40',
              res[1].strip().splitlines()[-1][:160])


def case_budget():
    print('9. kernel-size budget: the 16 MiB line the loader sits on (B4)')
    res = run_gates('--help')
    if res is None:
        note_skip('budget cases', 'scripts/rx2lpro-image-gates.py absent')
        return
    gates = res[2]
    vendor_kernel = 13384292      # measured, case 8 / LZMA-alone header at +0x2818
    for size, loader, should_pass, why in (
            (vendor_kernel, START, True,
             'the vendor kernel, under our 0x81000000 loader'),
            (vendor_kernel, 0x80d00000, True,
             'the vendor kernel under the vendor 0x80d00000 loader (247 KiB spare)'),
            (13700000, 0x80d00000, False,
             'a 313 KiB kernel bump bricks the vendor geometry'),
            (13700000, START, True,
             'the same bump is free at 0x81000000'),
            (16777216, START, False,
             'exactly 16 MiB overruns the running loader'),
            (17000000, START, False,
             'a 6.18 kernel that grew too big')):
        # run the real gate binary, not a re-implementation
        fd, path = tempfile.mkstemp(prefix='kbin-')
        os.close(fd)
        with open(path, 'wb') as fh:
            fh.truncate(size)
        rc, out, _ = run_gates('kernel', '--image', path,
                               '--loadaddr', hex(LOADADDR),
                               '--loader-start', hex(loader),
                               '--reserve', '64k')
        check((rc == 0) == should_pass,
              'gate %s for %d bytes at %#x (%s)'
              % ('passes' if should_pass else 'fails', size, loader, why),
              out.strip().splitlines()[-1][:150] if out.strip() else '')
        os.unlink(path)
    # budget gate on the finished image
    fd, path = tempfile.mkstemp(prefix='img-')
    os.close(fd)
    with open(path, 'wb') as fh:
        fh.truncate(15872 * 1024 + 1)
    rc, out, _ = run_gates('budget', '--image', path, '--limit', '15872k',
                           '--erase-size', '64k', '--pad')
    check(rc != 0, 'budget gate refuses an image 1 byte over the mtd3 window',
          out.strip().splitlines()[-1][:150] if out.strip() else '')
    os.unlink(path)
    print('      (gate binary: %s)' % gates)


def case_stock_dump():
    print('8. the loader rule, pinned against genuine vendor bytes')
    dump = next((p for p in STOCK_DUMP_CANDIDATES if os.path.exists(p)), None)
    if dump is None:
        note_skip('stock-dump cases', 'no KernelFS dump on this machine')
        return
    d = open(dump, 'rb').read()
    print('      %s (%#x bytes)' % (dump, len(d)))
    sig = d[:4]
    start, burn, ln = struct.unpack('>III', d[4:16])
    check(sig == b'cr6c', 'stock magic', repr(sig))
    check(burn == BURN, 'stock burn_addr == our CVIMG_KERNEL_BURN_ADDR',
          '%#x' % burn)
    ok, why = loader_gate(d)
    check(ok, 'B2 zero-sum holds on the vendor container', why)
    check(ln == 0x3c0802, 'stock len field', '%#x' % ln)
    check(d[HDR + ln:HDR + ln + 4] == b'nice',
          'stock rootfs sits at exactly 16+len (magic mangled by their kernel, '
          'not by the container)', '%#x' % (HDR + ln))
    # the LZMA stream's own declared size is the number the budget is about
    props, dictsize, usize = d[0x2818], struct.unpack('<I', d[0x2819:0x281d])[0], \
        struct.unpack('<Q', d[0x281d:0x2825])[0]
    check(props == 0x5d and dictsize == 0x800000,
          'vendor LZMA-alone props/dict at container+0x2818',
          'props=%#02x dict=%#x' % (props, dictsize))
    check(usize < START - LOADADDR,
          'vendor decompressed kernel %d < %d (our budget)'
          % (usize, START - LOADADDR),
          '%.3f MiB used' % (usize / 1048576.0))
    res = run_gates('container', dump, '--expect-burn', hex(BURN),
                    '--allow-mangled')
    if res:
        check(res[0] == 0, 'gates accept a stock dump only with --allow-mangled',
              res[1].strip().splitlines()[-1][:150])
    res = run_gates('container', dump, '--expect-burn', hex(BURN))
    if res:
        check(res[0] != 0, 'gates refuse the mangled magic by default',
              res[1].strip().splitlines()[-1][:150])
    res = run_gates('lzma-size', dump, '--at', '0x2818')
    if res:
        check(res[0] == 0, 'gates read the vendor uncompressed size',
              res[1].strip()[:150])


def main():
    tmp = tempfile.mkdtemp(prefix='rx2l-test-')
    cvimg, wrap = build_tools(tmp)

    case_basic_containers(tmp)

    plain = os.path.join(tmp, 'synthetic-cr6c.bin')
    data, payload, len_field = synth(0x1200)
    open(plain, 'wb').write(data)

    case_cvimg_writer(cvimg, tmp)
    case_loader_negatives(tmp)
    case_tenda(wrap, plain, tmp)
    case_corruption(tmp, open(plain, 'rb').read())
    case_formula()
    case_budget()
    case_stock_dump()

    print('\n%s   (%d failure%s, %d gap%s, %d skip%s)' %
          ('ALL PASS' if not fails else 'FAILED: ' + ', '.join(fails),
           len(fails), '' if len(fails) == 1 else 's',
           len(gaps), '' if len(gaps) == 1 else 's',
           len(skips), '' if len(skips) == 1 else 's'))
    if gaps:
        print('gaps (not build-breaking, listed for the checker owner): %s'
              % ', '.join(gaps))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
