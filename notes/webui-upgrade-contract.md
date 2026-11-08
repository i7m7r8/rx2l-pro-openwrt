# The webui "Local Upgrade" contract, as actually executed

Resolved 2026-11-07, corrected the same day. This replaces the open question that
used to sit at the end of the "Install paths" section of the README.

**Bottom line (second correction, 2026-11-07 evening): the webui CAN install a
hand-built image, if the veto path is made uncreatable first.** Two earlier
readings of this file were wrong in opposite directions, so both corrections are
kept on the record:

1. first: "the RSA verdict is discarded, so plaintext installs" — wrong, because
   `decry_firm` records its verdict in a flag file that `check_fw` tests first;
2. then: "therefore the webui can never take our image" — also wrong, and this
   is the subtle one. The flag is created with `creat()`, while `check_fw` tests
   it with `access()`, and the two disagree about a dangling symlink:
   `access()` reports ENOENT, `creat()` fails with ENOENT, so **no flag is ever
   created** and `check_fw` proceeds to its field checks, where a well-formed
   our-shaped container passes and the vendor flasher runs.

The signature is still unforgeable — that half of the analysis stands and is
documented in `port/08-veto-paths/DECRY-FIRM-FORMAT.md`. What is bypassable is
not the signature but **the recording of its failure**. See "The veto is
bypassable" below for the exact preconditions and the qemu proof.

Everything below is kept because it still describes the container geometry and
the field-level checks; the verdict sections have been rewritten against the
disassembly, and `port/08-veto-paths/REPORT.md` carries the executed proof.

## The chain, with the evidence

`lib/libgo.so` is the webui's CGI layer (`src/cgi/upload.c` appears in its strings).
Its `upgrade()` handler (0x441ec, 1672 B) does, in order:

| addr | what it runs |
|---|---|
| `0x4439c` | `snprintf(... "mv '/tmp/%s' /var/image", uploaded)` → `doSystemCmd` |
| `0x443ec` | `doSystemCmd("decry_firm %s %s", "/var/image", "/tmp/UPGRADE_DECRY_FAIL_FLAG")` |
| `0x44414` | `stat64("/var/image")` — **the only branch tested here is whether the file still exists** |
| `0x44548` | `mmap64` the image |
| `0x445a4` | `systool_upgradefile_handle(mmap_ptr, size, type)` → `tpi_upload` → `check_fw("/var/image")` |
| `0x44708` | `if (handle_result == 0) { clean_force_upgrade_info(); systool_call_writeImage_delay_11ac(fw_type); }` |
| `0x44800` | `doSystemCmd("rm -rf /var/image")`, then `sleep 5 && reboot &` |

Two things follow directly from that listing:

1. `decry_firm` is called with **two arguments: the image and the failure-flag
   path** — not in/out files. So `decry_firm` decrypts *in place* and records
   failure by touching the flag file.
2. **`doSystemCmd`'s return value is never examined.** The next consumer of `$v0`
   is the `stat64` call. The flasher is gated only by `systool_upgradefile_handle`
   returning 0, and that function's whole job is `check_fw`.

So the RSA gate exists, runs, fails — and its verdict does **not** vanish with
the return value: `decry_firm` receives the flag path as `argv[2]` and writes
that file itself. The next consumer of the failure is therefore `check_fw()`,
one call deeper (`port/08-veto-paths/REPORT.md` §1.5 has the call chain and the
`0x44708 bnez` that skips the flash trigger).

## Proof, offline, on the real binary

Ran the stock verifier under `qemu-mipsel-static` against the extracted rootfs, on
a **copy** of the genuine decrypted RX2L Pro plaintext image (i.e. a file that is
already past the TDE01 layer — exactly what our build would upload):

```
$ sha256sum image.bin
d1ee92c0339dfaa451aaf4ae0f32638e1a097a50fb11f31ce32d5b3ae409bc70
$ qemu-mipsel-static -L $rootfs $rootfs/bin/decry_firm image.bin FAILFLAG
[decrypt_signed_digest][36] get signed_digest failed!
[decrypt_firmware][118] get signed2 failed!
openssl decode failed        # rc=255
$ sha256sum image.bin          # unchanged, byte for byte
d1ee92c0339dfaa451aaf4ae0f32638e1a097a50fb11f31ce32d5b3ae409bc70
$ ls -l FAILFLAG               # created, 0 bytes
```

`decry_firm` refuses the plaintext, writes the flag, and **leaves the image
untouched** — it never opens it for writing before verifying. So `/var/image`
still holds our complete image when `check_fw` runs. That is what made the
earlier revision of this note conclude the image would flash; what it settles is
only that the *bytes survive* the failed decrypt. Survival is not acceptance:
`check_fw`'s first instruction tests the flag that the same failed run created.

Also checked: `publish/phone/js/upgrade.js` has no filename/extension/model
validation (only Vue `modelValue` bindings), so the browser adds no gate of its
own.

## Why the signature cannot be satisfied either

The flag is the barrier the webui *sees*; the reason it is always there is a
second, independent one, and it is worth recording separately because "just
craft a container" is the obvious next idea. `decry_firm` performs **two**
RSASSA-PKCS1-v1_5/SHA-512 verifications over two different 512-byte RSA blocks:
an outer one over the AES ciphertext (`0x004..0x203` over `SHA512(0x204..EOF)`)
and an inner one carried *inside* the decrypted stream, over the payload. Both
comparisons are real `memcmp`s whose result is consumed (`0x4064f0` and
`0x406604`); the debug printers that look like the verification are dead code.

The AES layer, by contrast, is fully public: the passphrase is recovered at run
time by RSA *public* decryption of a blob embedded in the binary, so anyone can
build a well-formed ciphertext — and it will still be rejected with
`signed2 HASH failed` because the ciphertext is signed. The one code weakness
(the inner `memcmp` length is `rsa_data_len - 0x13` and is never pinned to 0x40,
so a 19-byte data field would compare zero bytes) needs a PKCS#1 block, which
needs the private key.

Format, addresses, the qemu matrix and the crack attempts:
[`../port/08-veto-paths/DECRY-FIRM-FORMAT.md`](../port/08-veto-paths/DECRY-FIRM-FORMAT.md).

## The veto is bypassable: `access()` and `creat()` disagree on a dangling link

The gate is not "the image fails to verify", it is "a 0-byte file exists at
`/tmp/UPGRADE_DECRY_FAIL_FLAG`". Those are different properties, and only the
second one is tested:

```
decry_firm (argv[2] = the flag path):
    if (access(argv[2], F_OK) == 0) unlink(argv[2]);   /* clear stale flag */
    rc = decrypt_firmware(argv[1]);
    if (rc) { ... fd = creat(argv[2], 0777); if (fd >= 0) close(fd); }
check_fw:
    if (access("/tmp/UPGRADE_DECRY_FAIL_FLAG", F_OK) == 0) return -1;  /* refuse */
    ... check_fw_type / check_mtd_size / check_fw_valid / check_fw_crc ... return 0
```

Make that path a **dangling symlink** and both calls disagree with the file they
are supposed to describe: `access()` follows the link and reports ENOENT, so
`check_fw` sees "no failure"; `creat()` also follows it and fails ENOENT, so no
flag is written. The image is left untouched either way. Measured under qemu on
the stock binary, same plaintext input, only `argv[2]` differing:

| argv[2] | decry_firm rc | flag afterwards | `access()` on it |
|---|---|---|---|
| a normal path | 255 | **created**, 0 bytes | present -> `check_fw` refuses |
| `ln -s /nonexistent-dir/f <path>` | 255 | **absent** | ENOENT -> `check_fw` proceeds |

and the input file is byte-identical to the plaintext it was fed, so what
`check_fw` then inspects is our container.

Preconditions, stated plainly: it needs a root shell to plant the symlink
(`/tmp` is tmpfs, so it also has to be re-planted after a reboot), and the
container still has to satisfy the field checks — `fw_type` 2 (byte `+0x20`),
`+0x24 == 0x00031053`, payload CRC32 at `+0x18`, total ≤ 15.5 MiB. Those are
exactly the fields `tools/tenda-wrap` writes and `tools/check-webui-bin.py`
verifies, and the build refuses to emit a container that fails them.

If you already have root, root shell + `tendaupgrade 2 1 <file>` is simpler and
needs no precondition (`notes/install-runbook.md`). The symlink route exists
because it lets the *vendor webui* drive the flash — one KernelFS write, reboot,
fresh squashfs — with the same bytes.

## What `check_fw` requires, field by field

Decompiled with the Ghidra engine Craft Anything installed
(`check_fw` out of `lib/libcommonprod.so`@`0x2a518`; `check_fw_type`,
`check_fw_valid`, `check_fw_crc`, `check_mtd_size` are the helpers it calls, and
`lib/libcommon.so` ships the helpers without `check_fw` itself).
`check_fw_valid` returns **1 = valid**; the others return `-1` on the accepted
path, so read the returns as "which branch matched", not "error code".

The function opens with the flag test, before any of the field checks:

```
0002a550  move   $a1, $zero          ; F_OK
0002a558  addiu  $a0, $v0, 0x61a8    ; '/tmp/UPGRADE_DECRY_FAIL_FLAG'
0002a55c  lw     $v0, -0x7e30($gp)   ; {access}
0002a564  jalr   $t9
0002a570  bnez   $v0, 0x2a5b0        ; absent -> continue to check_fw_type
0002a578  addiu  $v0, $zero, -1      ; present -> -1, no field is examined
0002a590  printf('[%s]:[%d]-----image decrypted failed---\n', 'check_fw', 0x1e6)
```

Then the field checks below, which is why they still matter as a description of
a well-formed container.

* `check_fw_type(buf, size)` — `size >= 0x40` else reject. Copies the first
  `0x44` bytes, then `ntohl(buf[0x00])` must be `0x27051956`, and the byte at
  **`+0x20`** selects the family: `2` (our images) or `4`, and it recognizes a
  `0x27051958`/type-4 variant plus a `0x48844884` container. For type 2 it also
  tests `ntohl(buf[0x40]) == 0x476f6431` ("God1") -- that is a *different* OEM
  container family; ours stays `cr6c`, which falls through to the accepted
  return. So: **`+0x20 == 0x02` and container magic `cr6c` is the shape our
  board's genuine images use**, and it must be preserved, not invented.
* `check_fw_valid(type, path)` — reads the `0x40`-byte header, and for
  `type == 6` additionally requires the dual-image layout
  (`size - payload_len == 0x80`, then a second header at `payload_len + 0x40`),
  logging `only boot image` / `second image error`. Types other than
  `{2,4,6}` hit `fw_type is not support:%d`. Then the decisive comparison:

  ```c
  GetValue("fw.product.id", buf);              /* from the CFG partition */
  if (ntohl(header[0x24]) == strtoul(buf, 0, 16))  return 1;   /* valid */
  ```

  Verified against two independent sources: our `mtd2_CFG.bin` contains
  `product.id=0x00031053`, and the genuine RX2L Pro plaintext image carries
  `00 03 10 53` at `+0x24`. **So `+0x24` is not opaque -- it is the product id,
  and it must equal this router's `fw.product.id`.** That also retires the old
  guess that `+0x04`/`+0x08` were hashes of the model string (they are not, and
  they are still copied verbatim).
* `check_fw_crc` — the payload CRC32 at `+0x18`, which `tools/tenda-wrap`
  already computes and `tools/check-webui-bin.py` re-verifies.
* `check_mtd_size` — bounds the image by the target partition.

`tools/check-webui-bin.py` now encodes all of this, was run against the genuine
image (passes) and against single-byte mutations of the CRC field and the
product-id field (each rejected on exactly the right rule), and can re-run the
`decry_firm` pass-through probe itself with `--probe --rootfs <extracted rootfs>`.

## The header we must reproduce

From the genuine decrypted plaintext of our own model (`dec/` copy, first 0x40
bytes, all big-endian):

```
+0x00 27 05 19 56          magic
+0x04 cb 70 1d c8          OEM id/version field (not a hash of any
+0x08 68 4f f8 74          product string we tried — copy it verbatim)
+0x0c 00 b8 bc 14          payload length            (12 106 836 total - 0x40)
+0x10 80 00 00 00
+0x14 80 8d 45 c0          kref
+0x18 a8 6f 8c 2b          CRC32 of the payload        <- check_fw_crc
+0x1c 05 05 02 03          (hardware/id codes -- copy verbatim)
+0x20 02                   image family (check_fw_type switches on this byte)
+0x24 00 03 10 53          fw.product.id, must equal CFG's product.id=0x00031053
+0x28 00 b8 bc 54          total length = payload + 0x40
+0x2c..0x3f 00             zero padding
+0x40 63 72 36 63          "cr6c" — the Realtek container starts here
```

`+0x04`/`+0x08` are *not* simple CRC32/MD5/SHA1 prefixes of `RX2LPro`,
`RX2LProV1.0`, `US_RX2LProV1.0re`, `AX2_PRO_V2`, `V16.03.51.17` or the full
package name (tested all of those with crc32/adler32/md5/sha1/sha256, both byte
orders). Since we cannot recompute them, the rule for the build is simple and
safe: **take a genuine RX2L Pro plaintext image and replace only the payload and
the length/CRC fields.** That is exactly the round-trip `tools/tenda-wrap`
already proves byte-for-byte.

## Still unverified (and how each gets settled)

* The precise per-field comparisons inside `check_fw_valid` / `check_fw_type`
  (which bytes must equal which `fw.product.id` / `fw_type` values, and whether a
  version-downgrade rule rejects us). These no longer gate us — the flag check
  runs first and a hand-built image never reaches them — but they still describe
  a well-formed container, and `tools/check-webui-bin.py` encodes what is known
  so a malformed artifact is caught before flash rather than on it.
* Whether `check_mtd_size` bounds the image by the **whole** 16 MiB flash or only
  the 15 MiB `KernelFS` region — matters because our image must also fit the
  `check-size` budget of the image recipe.
* The `fw_type` value the webui passes for our product (types `2/4/6` are the
  supported set; `6` is the dual-image one and our board has a single `KernelFS`,
  so it must not be 6) — read from `libgo.so`'s `fw_type` global and the CGI
  parameter that sets it.
* The real-device behaviour of a **stale** flag: `business_proc`, `netctrl` and
  `cwmpd` all name `UPGRADE_DECRY_FAIL_FLAG`, and none of them is on the
  `tendaupgrade` path we install with. `/tmp` is tmpfs, so the flag cannot
  survive a power cycle; what is still unread is whether any of them acts on one
  inside the same boot. This does not affect README path 2 (the vendor flasher
  never decrypts and has no `check_fw` import), only a hypothetical future
  webui-based flow.

## Install semantics — "erase userdata, then fresh OpenWrt"

The flasher writes exactly one partition: `KernelFS` (`/dev/mtd3`, flash
offset `0x40000`), via `tendaupgrade`, which flips `env image_boot` and reboots.
(The webui would have reached the same flasher through
`systool_call_writeImage_delay_11ac`, but it cannot get past `check_fw`; see the
top of this file.) Consequences for a clean install:

* Everything in OpenWrt's config lives in the **overlay**, which does not exist in
  a fresh squashfs image, so first boot runs all `/etc/uci-defaults/*` and lands on
  stock defaults — that is the "factory reset" the vendor UI implies, and it needs
  no extra erase.
* The vendor `CFG`/config partitions must **not** be erased: they carry this unit's
  MAC and the RF calibration data we read at runtime (see `port/FACTS.md` §5–§6).
  Our port reads them; it never writes them.
* A sysupgrade later replaces squashfs+overlay normally, and `firstboot` remains
  the explicit reset path.

## Two implementations of this contract, cross-checked

`tools/check-webui-bin.py` (manual inspector, referenced from this note and the
README) and `scripts/rx2lpro-image-gates.py webui` (the build gate, wired into the
`tenda-plaintext.bin` pipeline) both encode the rules above. Two encodings of one
contract can drift, so they were compared on three inputs:

| input | `check-webui-bin.py` | `gates.py webui` |
|---|---|---|
| the vendor's own decrypted OTA | `all rules passed` | exit 0, every check OK |
| one bit flipped at `+0x27` (product id) | `1 problem(s)` | exit 1, fails on `check_fw_crc` |
| a 16 MiB payload wrapped (header overflows mtd3) | `FAIL image is 0xf80040 bytes; KernelFS (mtd3) is 0xf80000` | `GATE FAIL [mtd-size]: total image 0xf80040 exceeds the KernelFS window` |

They agree on all three, including the reason: the third is `check_mtd_size` →
-3 on both sides, not two different tests happening to fail.

Division of labour from here: the build gate runs on every `tenda-plaintext.bin`
and fails the build; `check-webui-bin.py` stays the tool to reach for when
inspecting an arbitrary file by hand (`--reference` lets it compare the opaque
header words against a known-good image, which the gate does not attempt).
