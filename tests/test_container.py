#!/usr/bin/env python3
"""Self-test for the two container tools, with no vendor data involved.

Builds a synthetic cvimg 'cr6c' container (header + payload + checksum +
squashfs superblock), then proves:

  1. tools/check-cr6c.py accepts it;
  2. files/tools/firmware-utils/src/tenda-wrap.c reproduces the exact 64-byte
     Tenda plaintext header the vendor images carry (lengths and the payload
     crc32 included) and the wrapped file still passes the checker;
  3. one flipped byte in the payload is caught;
  4. both rootfs placements -- the exact 16+len offset the vendor uses, and the
     4-aligned one some containers pad to -- are located.

    python3 tests/test_container.py        # needs gcc for step 2's tool build
Run from the repo root.
"""
import os
import random
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CHECKER = os.path.join(ROOT, 'tools', 'check-cr6c.py')
WRAP_SRC = os.path.join(ROOT, 'files', 'tools', 'firmware-utils', 'src', 'tenda-wrap.c')

START = 0x81000000
BURN = 0x40000
KREF = 0x808d45c0
fails = []


def cvimg_checksum(buf):
    s = 0
    for i in range(0, len(buf) - 1, 2):
        s = (s + ((buf[i] << 8) | buf[i + 1])) & 0xFFFF
    if len(buf) % 2:
        s = (s + (buf[-1] << 8)) & 0xFFFF
    return struct.pack('>H', (~s + 1) & 0xFFFF)


def squashfs(total_bytes):
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
    return bytes(sb) + os.urandom(4096)


def container(payload_len, pad_to_align):
    """[cr6c hdr][payload][2-byte checksum][(optional padding)][squashfs]."""
    rnd = random.Random(payload_len)
    payload = bytes(bytearray(rnd.getrandbits(8) for _ in range(payload_len)))
    len_field = len(payload) + 2
    hdr = b'cr6c' + struct.pack('>III', START, BURN, len_field)
    body = hdr + payload + cvimg_checksum(payload)
    if pad_to_align:
        while len(body) % 4:
            body += b'\x00'
    root = squashfs(4096)
    return body + root, payload, len_field


def run_checker(path, tenda=False):
    cmd = [sys.executable, CHECKER]
    if tenda:
        cmd.append('--tenda')
    cmd += ['--expect-start', hex(START), path]
    p = subprocess.run(cmd, capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def check(ok, label, detail=''):
    print('  [%s] %s%s' % ('PASS' if ok else 'FAIL', label,
                           (' - ' + detail) if detail else ''))
    if not ok:
        fails.append(label)


def main():
    tmp = tempfile.mkdtemp(prefix='rx2l-test-')
    plain = os.path.join(tmp, 'synthetic-cr6c.bin')
    wrapped = os.path.join(tmp, 'synthetic-tenda.bin')

    print('1. plain container, payload 0x1200 (16+len is 4-aligned)')
    data, payload, len_field = container(0x1200, pad_to_align=False)
    open(plain, 'wb').write(data)
    rc, out = run_checker(plain)
    check(rc == 0, 'checker accepts it', 'rc=%d' % rc)
    check('exact offset' in out, 'took the exact 16+len candidate')

    print('2. container padded so only the ALIGN(16+len,4) candidate hits')
    data2, _, _ = container(0x1200, pad_to_align=True)
    aligned_path = os.path.join(tmp, 'aligned.bin')
    open(aligned_path, 'wb').write(data2)
    rc, out = run_checker(aligned_path)
    check(rc == 0, 'checker accepts the padded layout', 'rc=%d' % rc)
    check('exact offset' not in out, 'padded layout resolved on the aligned candidate')

    print('3. tenda-wrap reproduces the vendor header shape')
    tw = os.path.join(tmp, 'tenda-wrap')
    r = subprocess.run(['gcc', '-Wall', '-Wextra', '-Werror', '-O2',
                        '-o', tw, WRAP_SRC], capture_output=True, text=True)
    check(r.returncode == 0, 'tenda-wrap compiles clean', r.stderr.strip()[:200])
    if r.returncode == 0:
        r = subprocess.run([tw, '-i', plain, '-o', wrapped, '-k', hex(KREF)],
                           capture_output=True, text=True)
        check(r.returncode == 0, 'tenda-wrap runs', (r.stderr or r.stdout).strip()[:200])
        w = open(wrapped, 'rb').read()
        head = w[:0x40]
        check(head[:4] == b'\x27\x05\x19\x56', 'magic')
        got_plen, got_kref, got_crc, got_tlen = struct.unpack('>IIII',
                                                              head[0x0c:0x10] + head[0x14:0x18] +
                                                              head[0x18:0x1c] + head[0x28:0x2c])
        check(got_plen == len(data), 'payload-length == container size',
              '%#x vs %#x' % (got_plen, len(data)))
        check(got_tlen == len(w), 'total-length == wrapped size')
        check(got_crc == zlib.crc32(data) & 0xFFFFFFFF, 'crc32 field == crc32(payload)',
              '%#010x' % got_crc)
        check(got_kref == KREF, '-k lands at +0x14')
        check(w[0x10:0x14] == b'\x80\x00\x00\x00', '+0x10 constant')
        check(w[0x1c:0x24] == bytes.fromhex('0505020302000000'), '+0x1c constants')
        check(w[0x24:0x28] == bytes.fromhex('00031053'), '+0x24 constant')
        check(w[0x2c:0x40] == b'\x00' * 0x14, 'tail zeros')
        check(w[0x40:] == data, 'container passed through unchanged')
        rc, out = run_checker(wrapped, tenda=True)
        check(rc == 0, 'checker accepts the wrapped file', 'rc=%d' % rc)

    print('4. one flipped byte must be caught')
    bad = bytearray(data)
    bad[0x40 + 500] ^= 0xFF
    bad_path = os.path.join(tmp, 'corrupt.bin')
    open(bad_path, 'wb').write(bytes(bad))
    rc, out = run_checker(bad_path)
    check(rc != 0, 'corrupted payload is rejected')
    check('checksum' in out, 'the rejection names the checksum')

    print('5. cvimg checksum matches the vendor formula on a known vector')
    # the vendor container's own payload/checksum pair, taken from the spec doc:
    # 0x3c0800 bytes of kernel with checksum 8161. We cannot ship the payload,
    # so instead pin the implementation against an independent CRC-of-16-bit-words
    # written a different way (sum over struct-unpacked big-endian halves).
    vec = bytes(bytearray(range(256))) * 4
    alt = sum(struct.unpack('>H', vec[i:i + 2])[0] for i in range(0, len(vec), 2)) & 0xFFFF
    check(cvimg_checksum(vec) == struct.pack('>H', (~alt + 1) & 0xFFFF),
          'checksum implementation agrees with an independent form')

    print('\n%s' % ('ALL PASS' if not fails else 'FAILED: ' + ', '.join(fails)))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
