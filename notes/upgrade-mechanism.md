# The RX2L Pro firmware-upgrade mechanism, end to end

One document, every stage, every address. It consolidates what the scattered
notes already prove and adds the loader half that was missing
(`notes/bootloader-tde01.md`). Device: Tenda RX2L Pro / TX2L Pro, RTL8197F,
16 MiB SPI NOR, 128 MiB RAM, MIPS32 **little-endian** userspace over a
**big-endian** MIPS bootloader.

---

## 0. Answer first

| question | answer |
|---|---|
| Can the stock webui take a self-built image? | **Only via one precondition**: a dangling symlink at `/tmp/UPGRADE_DECRY_FAIL_FLAG` (§4). |
| Can any crafted file satisfy the signature? | **No.** Two real RSASSA-PKCS1-v1_5/SHA-512 `memcmp`s, in *both* `decry_firm` and stage-1. |
| Is the AES layer a barrier? | **No.** The passphrase is RSA-*public*-decryptable from a blob in each binary (§2.3). |
| Does the bootloader skip the signature? | **No** — but it only checks it on the *burn* path, not the *boot* path (§5, §6). |
| What does flashing actually write? | Only `firmware`/mtd3 `0x040000..0xFC0000`. CFG/CFM/MISC2 are untouched **unless** you go through `upgrade_wait` (§6.4). |
| Is "erase userdata" a separate step? | No. Fresh squashfs + uci-defaults *is* the factory reset. |

---

## 1. Flash map and the four moving parts

```
mtd0 ALL        16 711 680  @0x000000
mtd1 Bootloader    196 608  @0x000000   BE MIPS stage-1 + U-Boot, XIP @0xb0000000
mtd2 CFG            65 536  @0x030000   ASCII KVs: MACs, fw.product.id, RF
mtd3 KernelFS   16 252 928  @0x040000   <-- the only thing any install writes
mtd4 RootFS     12 318 190  @0x400812   squashfs, carved OUT of mtd3 by mtdsplit
mtd5 CFM            65 536  @0xFC0000   RF calibration (read at runtime!)
mtd6 CFM_BACKUP     65 536  @0xFD0000
mtd7 MISC2          65 536  @0xFE0000
```

Moving parts, in the order a byte meets them:

1. **Upload gate** — userspace `/bin/decry_firm` + `check_fw()` in
   `libcommonprod.so`. Decides whether the file is accepted at all.
2. **TDE01 wrapper** — 512 B RSA signature + `Salted__` AES-CBC, with an
   *inner* 512 B signature inside the plaintext. See §2.
3. **Tenda plaintext header** — 64 B in front of the container. See §3.
4. **`cr6c` cvimg container** — 16 B header, LZMA stub + kernel, 2-byte
   checksum, squashfs at `16+len`. See §3.
5. **Loader** — verifies on burn (§5), refuses on boot only on magic + sum (§6).

---

## 2. Layer 1: the TDE01 wrapper

### 2.1 Geometry

```
+0x000   4    BE total length (informational in userspace; the loader reads it)
+0x004 512    RSA signature #1 over SHA512(+0x204 .. EOF)
+0x204   8    "Salted__"
+0x20C   8    salt
+0x214   n    AES-128-CBC ciphertext of [512-byte sig #2][payload][pad]
```

`len = st_size`; `L = len - 0x214`; the plaintext is `sig2(512) || payload ||
pad`, `pad` = last plaintext byte, and only that byte is read as the pad length.

### 2.2 The two checks

```
outer: memcmp(SHA512(bytes 0x204..EOF), sig1 + 0x13, l1 - 0x13)
inner: memcmp(SHA512(payload),           sig2 + 0x13, l2 - 0x13)
```

`0x13` is the 19-byte SHA-512 `DigestInfo` prefix
`30 51 30 0d 06 09 60 86 48 01 65 03 04 02 03 05 00 04 40`. The prefix itself is
never validated; the 64 digest bytes at `+0x13` are.

Both `memcmp`s return into a branch that aborts:

| where | address | string on failure |
|---|---|---|
| userspace outer | `0x4064e8` → `0x4061fc` → `memcmp@0x406270`, branch `0x4064f0` | `[decrypt_firmware][124] signed2 HASH failed!` |
| userspace inner | `0x4065fc`, branch `0x406604` | `[decrypt_firmware][140] signed2 HASH failed!` |
| loader outer | `FUN_80019d08` | `Verification Failure! -> encrypted_signed_firmware` |
| loader inner | `FUN_80019d08` | `Verification Failure! -> firmware` |

### 2.3 The key material — public, and duplicated

| | `decry_firm` | stage-1 |
|---|---|---|
| N (4096-bit, little-endian limbs) | `.rodata 0x41cd00` | `0x8002d2b0` |
| e | `.data 0x42f940` = `0x10001` | `FUN_80019af0` stores `0x10001` |
| AES-password blob | `.data 0x42f740` | `0x80035bd0` |

Both moduli are the *same* 512 bytes (sha256
`84a1c9d8…756ad71`), both blobs are the same 512 bytes (sha256
`d92ca383…3651e6c5`). RSA-public-decrypting that blob yields the 90-byte
plaintext

```
TZVzOlV0tJnQ5mkSJu9jVx4IN62DE1mT1blmfYteqZ9LFNeIfZzrNmhSBVmeHQHS
t/GU20StkNunubF4Xb8ctQ==
```

of which the first 64 bytes are the OpenSSL passphrase, and

```
key = MD5(pass || salt)
iv  = MD5(key || pass || salt)     # EVP_BytesToKey(MD5), 1 iteration, AES-128-CBC
```

Re-encrypting a genuine plaintext with this key/IV reproduces the genuine
ciphertext byte-for-byte — verified. **The AES layer is therefore free to
anyone; the signatures are not.**

### 2.4 The one code weakness, and why it is not reachable

`memcmp` length is `rsa_data_len - 0x13`, never pinned to `0x40`. A PKCS#1 v1.5
block carrying exactly 19 data bytes would make the compare zero-length and
pass. Producing any such block requires `m^d mod N`; `N` is a genuine 4096-bit
composite (Miller-Rabin composite, `gcd(N,e)=1`, not a square, no factor below
10⁶). `rsa_data_len < 0x13` underflows to ~4 GiB and crashes rather than passes.

---

## 3. Layers 2 and 3: the plaintext header and the `cr6c` container

### 3.1 The 64-byte Tenda header (output of `decry_firm`, input to the flasher)

```
+0x00 27 05 19 56      uImage magic — the loader uses THIS to find the header
+0x04 cb 70 1d c8      per-build id      (not content-derived, not verifiable)
+0x08 68 4f f8 74      per-build epoch   (idem)
+0x0c 00 b8 bc 14      payload length = filesize - 0x40
+0x10 80 00 00 00      constant
+0x14 80 8d 45 c0      kernel reference  (idem; RX vs TX differ by 0x10)
+0x18 a8 6f 8c 2b      CRC32(payload)    <- check_fw_crc
+0x1c 05 05 02 03      constant
+0x20 02               fw_cls = 2 = FW_TYPE_KERNEL_IMAGE  <- check_fw_type
+0x21 00 00 00         pad
+0x24 00 03 10 53      fw.product.id    <- check_fw_valid, must equal CFG's
+0x28 00 b8 bc 54      total length = payload + 0x40
+0x2c ..0x3f  00       zeros
```

Reproduced byte-exactly by `tools/tenda-wrap`; the round trip is
`tests/test_container.py`. Three fields (+0x04, +0x08, +0x14) are not derivable
from content and therefore cannot be checked by anything — they are copy-through
values from the product's own image.

### 3.2 The `cr6c` container (standard Realtek `cvimg`)

```
+0x00  4   "cr6c"                      DATA_FIRMWARE | CPU_NEW  (cs6c = kernel only)
+0x04  4   BE start_addr               jump target == the stub's link address
+0x08  4   BE burn_addr                byteswapped, unused
+0x0c  4   BE len                      = payload_len + 2
+0x10  len-2  payload                 LZMA stub + vmlinux.lzma
16+len-2  2  checksum                 htons(~sum16 + 1), BE
16+len     …  squashfs                stock mangles `hsqs` → `nice`
```

The loader's own identity, verified on the loader image:

```
sum of BE u16 words over `len` bytes from container+0x10 == 0 mod 2^16
```

Genuine sample: `len = 0x3c0c02`, payload `0x3c0c00`, checksum `435c` at
`0x3c0c10`, `nice` at `0x3c0c12`, and the sum over the `len` bytes from `+0x10`
is exactly `0x0`. Our build must emit plain `hsqs` (our kernel has no vendor
magic patch), which is also what our `mtdsplit` matches.

### 3.3 The header's 64-byte skip — solved

`FUN_80001d48` in stage-1 walks 16-byte records. When the first record matches
the uImage magic (`.data 0x80028f80` = `27 05 19 56`) it sets `local_78 = 0x40`
and advances the record pointer by 0x40 before looking for `cr6c`. That is the
skip `install-runbook.md` had listed as unexplained.

---

## 4. The userspace gate: `decry_firm` + `check_fw`

### 4.1 `decry_firm` — what it does

```
main(argv):
    if argv[2] && access(argv[2], F_OK) == 0: unlink(argv[2])   # clear stale flag
    rc = decrypt_firmware(argv[1])
    if rc == 0: return 0
    puts("openssl decode failed")
    if argv[2]: fd = creat(argv[2], 0777); close(fd)
    return -1                                                   # exit status 255
```

`decrypt_firmware` opens `O_RDWR|O_CREAT`, `fstat`s, requires `S_ISREG`, `mmaps`,
then runs the TDE01 checks of §2 and finally
`memmove(base, base+0x200, payload_len)` + `ftruncate`. **It decrypts in place**,
so a *successful* run destroys the wrapper; a *failed* run leaves the file
untouched.

### 4.2 The upstream chain

```
libgo.so upgrade() @0x441ec
  0x4439c  mv /tmp/<upload> /var/image
  0x443ec  decry_firm /var/image /tmp/UPGRADE_DECRY_FAIL_FLAG   (rc ignored!)
  0x44414  stat64("/var/image")
  0x445a4  systool_upgradefile_handle → tpi_upload → check_fw("/var/image")
  0x44708  if (handle_result == 0) → writeImage_delay_11ac → reboot
  0x44800  rm -rf /var/image; sleep 5; reboot
```

`check_fw` (`libcommonprod.so@0x2a518`) is gated **first** on the flag file:

```
0x2a564  access("/tmp/UPGRADE_DECRY_FAIL_FLAG", F_OK)
0x2a570  bnez $v0, 0x2a5b0          # absent -> field checks
0x2a578  return -1                  # present -> refuse, "image decrypted failed"
```

then `check_fw_type` (magic `0x27051956` + `fw_cls` 2 → type 2), `check_fw_valid`
(`+0x24 == fw.product.id` from CFG = `0x00031053`), `check_fw_crc`
(`+0x18 == crc32(payload)`), `check_mtd_size` (≤ `0xF80000`).

So the file-level gate is: **shape + CRC + product id + size**, none
cryptographic. The cryptographic gate is entirely in `decry_firm`, and what
`check_fw` actually sees is only *whether that gate wrote a flag*.

### 4.3 The bypass, stated exactly

`decry_firm` **creates** the flag with `creat()`; `check_fw` **tests** it with
`access()`. On a **dangling symlink** both calls follow it and both fail:

| argv[2] is | `decry_firm` rc | flag after | `access()` | `check_fw` |
|---|---|---|---|---|
| a normal path | 255 | created, 0 B | F_OK ok | refuse |
| `ln -s /nonexistent/x PATH` | 255 | **absent** | ENOENT | **proceed** |

Measured under qemu on the stock binary, same plaintext input, only `argv[2]`
differing. Preconditions: root shell to plant it, and re-planting after reboot
because `/tmp` is tmpfs.

Bootstrapping question this raises: if you already have a root shell you do not
need the webui at all — `tendaupgrade 2 1 <file>` is shorter and has no
precondition. The symlink route exists because it lets the *vendor page* drive
the flash, not because it is easier.

### 4.4 What `tendaupgrade` is

`main @0x402388`: `argc ∈ {4, 2}`; `argv[1]` → fw type where **only 2 flashes**
(the 0..6 range check at `0x40255c` is a coarse filter, the worker
`FUN_00401fdc` requires `== 2`); `argv[2]` → reboot flag 0/1; `argv[3]` → path
(0x40 max). Worker: resolve partition **by name** from `/proc/mtd`, unlock →
erase → write → `env set image_boot` → reboot. **No magic, checksum, size or
signature check anywhere in the flasher.** It contains neither the
`decry_firm` string nor any `check_fw` import.

---

## 5. The loader half (new)

Full detail in `notes/bootloader-tde01.md`. Summary:

* **Burn path** (`FUN_80001d48`): if the buffer does not begin with `"boot"` it
  is TDE01-verified with the *same* N, e and AES blob, then walked as records
  (`cr6c` / `cs6c` / `w6cg` / `r6cr` / `boot` / `ALL1` / `ALL2` / `nice`).
  A bare `cr6c` container therefore **fails** here with `decryption err`.
* **Boot path** (`FUN_80010be4`): checks only magic ∈ {`cr6c`, `cs6c`} and the
  BE-u16 zero-sum. No RSA, no squashfs test, no KM. `cs6c` additionally triggers
  a rootfs check (`FUN_800109e4`) that our `cr6c` shape never reaches.
* **Destructive branch** (`FUN_80003858`): on success with
  `_DAT_8003826c != 0` it logs `erase restore factory partition......` and
  erases `0xfc0000..0xfe0000` — **CFM and CFM_BACKUP**, the RF calibration.

---

## 6. Install paths, ranked

| # | path | needs | writes | notes |
|---|---|---|---|---|
| 1 | **RAM boot** `*-initramfs-kernel.bin` via `XMOD`+`J` | serial | nothing | always safe; the only zero-risk proof |
| 2 | **Webui Local Upgrade** + `*-tenda-plaintext.bin` | dangling symlink (§4.3) | mtd3 | vendor UI drives it; re-plant symlink each boot |
| 3 | **`tendaupgrade 2 1 <file>`** | root shell | mtd3 | no precondition, no flag, no decrypt |
| 4 | **Loader `AUTOBURN`/TFTP** | serial + TFTP | mtd3 | wants a TDE01 image or a `boot`-led record container, *not* bare `cr6c` (§5) — VERIFY before trusting |
| 5 | **Direct `mtd write` to mtd3** | root shell | mtd3 | the explicit fallback |
| 6 | **`sysupgrade -n`** | *this* port already running | firmware | gated by our own `platform.sh` |

"Erase userdata" for all of 1-6: a fresh squashfs has no overlay, so first boot
runs every `/etc/uci-defaults/*` and lands on defaults. That *is* the wipe. CFG
and CFM must never be touched — they carry this unit's MACs and calibration.

---

## 7. The OpenWrt 6.18 image we are producing

`target/linux/rtl819x/image/rtl8197f.mk`, device `tenda_rx2lpro`:

```
KERNEL := kernel-bin | append-dtb | rx2lpro-kernel-gate | lzma
        | rx2lpro-loader | cvimg-kernel-cr6c
CVIMG_LOADER_START_ADDR := 0x82000000     # 32 MiB window; == header start_addr
CVIMG_KERNEL_BURN_ADDR  := 0x40000
CVIMG_TENDA_KREF        := 0x808d45c0
IMAGE_SIZE              := 15872k         # 0xF80000, the mtd3 window
BLOCKSIZE               := 64k

IMAGE/factory-cr6c.bin     := append-kernel | append-rootfs | budget | pad-rootfs | budget
IMAGE/tenda-plaintext.bin  := ... | tenda-wrap | webui-gate
IMAGE/sysupgrade.bin       := ... | budget | pad-rootfs | budget | append-metadata | check-size
```

Gates that must be green: kernel budget (`loader_start - loadaddr - 64k`),
`cr6c` + BE-u16 zero-sum, squashfs at exactly `16+len`, Tenda header product id
`0x00031053` and payload CRC32, and total ≤ `0xF80000`.

`platform.sh` re-checks before erasing: `get_magic_long == 63723663` and a plain
`hsqs`/`sqsh` at `16+len`. A vendor container is refused by design (its rootfs
magic is `nice`), so the sysupgrade path cannot be used to downgrade to stock —
that needs path 2/4/5.

---

## 8. Where the build stands (2026-11-10)

`gh run list` at the time of writing:

```
in_progress  build  rebase-latest  workflow_dispatch  42795454409   (selftest ✓)
completed    failure 42795091652 (5s)  — selftest never started
completed    failure 42787889525 (5s)  — selftest never started
completed    failure 42560868515 (1h50m) — tenda-wrap not in tools-y
completed    failure 42464008953 (2h34m) — same, the run that found it
completed    failure 42453851939 (1h33m) — install stage hid its error
completed    failure 42447250472 (1h29m) — first clean kernel compile
```

The two 5-second failures are **not code**: the check-run annotation reads

> The job was not started because recent account payments have failed or your
> spending limit needs to be increased.

So from `42560868515` onward the real question is only whether the image stage
now finds `tenda-wrap` (fixed by `772e0af` + `compat/host/0001-…patch`), and
whether `e0b0ddd`'s squashfs block-size pin satisfies the container gate. Run
`42795454409` is the first to get past selftest since then.

---

## 9. Read-only proof commands (no hardware, no build)

```sh
# container geometry of any candidate file
python3 tools/check-cr6c.py --tenda IMAGE.bin

# the webui's own four checks, offline
python3 scripts/rx2lpro-image-gates.py webui IMAGE.bin --expect-cls 2 \
        --product-id 0x00031053 --kernelfs-max 0xf80000 \
        --expect-start 0x82000000 --expect-burn 0x40000

# reproduce the loader's BE-u16 zero-sum by hand
python3 - <<'PY'
import struct
d=open('IMAGE.bin','rb').read()[0x40:]          # skip the 64-byte Tenda header
ln=struct.unpack('>I',d[12:16])[0]
s=sum(((d[0x10+i]<<8)|d[0x10+i+1]) for i in range(0,ln-1,2))&0xffff
print('len',hex(ln),'sum',hex(s),'PASS' if s==0 else 'FAIL')
PY
```
