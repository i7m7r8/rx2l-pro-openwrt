# The webui "Local Upgrade" contract, as actually executed

Resolved 2026-11-07. This replaces the open question that used to sit at the end
of the "Install paths" section of the README.

**Bottom line: a Tenda-webui-uploadable OpenWrt image is possible, and the way it
works is that the signature check is real but *not on the critical path*.** The
webui runs the vendor RSA verifier, ignores its result, and then flashes whatever
`/var/image` still contains once a *format* check passes.

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

So the RSA gate exists, runs, fails — and is discarded.

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
still holds our complete image when `check_fw` runs. This is the single experiment
that turns "webui-upgradable" from speculation into a property of the code.

Also checked: `publish/phone/js/upgrade.js` has no filename/extension/model
validation (only Vue `modelValue` bindings), so the browser adds no gate of its
own.

## What `check_fw` actually requires

`check_fw` (libgo.so 0x42758, and a second copy in `libcommonprod.so`) calls, in
order, `check_fw_type`, `check_fw_valid`, `check_fw_crc`, `check_mtd_size` — all
four defined in `lib/libcommon.so` (`check_fw_type@0xa824`, `check_mtd_size@0xbd60`,
`check_fw_crc@0xb618`, `check_fw_valid@0xc42c`). There is **no** RSA/`EVP_DigestVerify`
symbol anywhere in that cone. Its log strings name the criteria:

* `check_fw_type`: `arg is null.`, `filebuf is too smaller. size is %ld`
* `check_fw_valid`: `read image header is fail.`, `get file size fail`,
  `only boot image`, `second image error`, `fw_type is not support:%d.`,
  `fw.product.id` / `get fw product id is fail.`
* `check_cmp_policy_file`: it also unpacks and compares `/etc/policy.cfg.lzma`
  (`/bin/lzma d`, `/bin/mv`, `/etc/policy.old`) — the OEM "policy" blob, separate
  from the image.

Note the dual-image vocabulary (`only boot image`, `second image error`) even
though **our board has only one `KernelFS`** — the validator is shared OEM code,
so `fw_type` selects how many images it expects. Getting `fw_type` right is part
of matching a genuine image (the value is handed to the flasher as
`systool_call_writeImage_delay_11ac(fw_type)` and `fw_type` is a global).

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
+0x1c 05 05 02 03 02 00 00 00
+0x24 00 03 10 53
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
  version-downgrade rule rejects us). Ghidra is decompiling those four functions
  now; the answer goes in this file, and the result becomes a CI assertion rather
  than a guess.
* Whether `check_mtd_size` bounds the image by the **whole** 16 MiB flash or only
  the 15 MiB `KernelFS` region — matters because our image must also fit the
  `check-size` budget of the image recipe.
* The real-device behaviour of the flag file: does some *other* daemon
  (`business_proc`, `netctrl`, `cwmpd` all read `UPGRADE_DECRY_FAIL_FLAG`) veto or
  roll back the flash after `check_fw` passed? None of them is on the `upgrade()`
  path, but this must be read before we trust the conclusion on hardware.

## Install semantics — "erase userdata, then fresh OpenWrt"

The webui path writes exactly one partition: `KernelFS` (`/dev/mtd3`, flash
offset `0x40000`), via `tendaupgrade`, which flips `env image_boot` and reboots.
Consequences for a clean install:

* Everything in OpenWrt's config lives in the **overlay**, which does not exist in
  a fresh squashfs image, so first boot runs all `/etc/uci-defaults/*` and lands on
  stock defaults — that is the "factory reset" the vendor UI implies, and it needs
  no extra erase.
* The vendor `CFG`/config partitions must **not** be erased: they carry this unit's
  MAC and the RF calibration data we read at runtime (see `port/FACTS.md` §5–§6).
  Our port reads them; it never writes them.
* A sysupgrade later replaces squashfs+overlay normally, and `firstboot` remains
  the explicit reset path.
