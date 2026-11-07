# Tenda plaintext image header (the 64 bytes in front of the cr6c container)

Established from two decrypted vendor upgrades — RX2L Pro `V16.03.51.17` and
TX2L Pro `V16.03.51.14` — i.e. the exact output of the device's own
`/bin/decry_firm`. `tools/tenda-wrap.c` reproduces a real vendor image with
**zero differing bytes**; `tests/test_container.py` guards that.

| off | size | field | evidence |
|-----|------|-------|----------|
| 0x00 | 4 | `27 05 19 56` | constant (uImage magic) |
| 0x04 | 4 | BE u32, per-build | differs rx vs tx; not content-derived → cannot be a checksum |
| 0x08 | 4 | BE u32, per-build | differs; tx reads as a 2022-10-17 epoch, rx does not |
| 0x0c | 4 | **payload length** (BE) | `0xb8bc14` = file size − 0x40, both samples |
| 0x10 | 4 | `0x80000000` | constant |
| 0x14 | 4 | BE u32 kernel reference | rx `0x808d45c0`, tx `0x808d45d0` — differs by exactly 0x10 |
| 0x18 | 4 | **CRC32 of the payload** (BE) | verified: both samples, `zlib.crc32(data[0x40:])` |
| 0x1c | 8 | `05 05 02 03 02 00 00 00` | constant |
| 0x24 | 4 | `0x00031053` | constant |
| 0x28 | 4 | **total file length** (BE) | `0xb8bc54` = payload + 0x40, both samples |
| 0x2c | 20 | zeros | |

## What this changes

The CRC32 at +0x18 is the important one: it is a *post-decryption* integrity
field, so it is computable by anyone and guards nothing. Combined with the
cr6c finding (whose only integrity field is a 16-bit additive checksum), the
conclusion is:

> **Nothing below the TDE01 wrapper is a security boundary.** The RSA-4096 /
> SHA-512 signature over the AES payload — i.e. the wrapper itself — is the
> *only* gate. Break it and you need nothing else; without it, the inner
> formats are all freely reproducible.

So an image we build is byte-format-identical to a genuine decrypted vendor
image in every field except the three per-build words at +0x04/+0x08/+0x14,
which are not derivable from content and therefore cannot be verified against
content. If the vendor flasher rejects our file at all, it is doing so on a
heuristic (model string, version compare, or that +0x14 reference), not on a
cryptographic property.

## Not established

* Whether `tendaupgrade` reads or validates this header at all — it is
  normally the *output* of the decrypt step, not an input to the flasher.
* The meaning of +0x04/+0x08 (build id? signing epoch?) and +0x14 (kernel
  reference — an address inside the vendor kernel, most likely).
* Whether any Tenda model uses cvimg's `-T` checksum mode ("a total image
  length 16 bytes shorter", per cvimg's own help). The RX2L Pro's stock
  container does **not** — standard format, verified.
