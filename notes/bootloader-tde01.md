# The bootloader carries the same RSA key — and verifies TDE01 itself

Source artifact: the decompressed stage-1 image from this unit's own `mtd1`
(gzip stream at mtd1 `+0x8d60`), staged as `/tmp/mtd1_stage1.elf`. It is an
ELF32 **big-endian** MIPS object linked at `0x80000000`; the single PT_LOAD
segment is file offset `0x1000`, size `0x380d0` = **229,584 bytes** (the number
`port/FACTS.md` §3 already quotes). Every address below is a *virtual* address
in that image; subtract `0x80000000` and add `0x1000` to read it out of the
file.

This note exists because `notes/tde01-verdict.md` and
`port/08-veto-paths/DECRY-FIRM-FORMAT.md` describe the gate as if it lived only
in userspace `/bin/decry_firm`. It does not: **the loader has the same key
material and the same two-signature check.** That closes the last "maybe the
bootloader is the soft path" question.

---

## 1. Same key material, byte for byte

| what | in `decry_firm` | in stage-1 | identical |
|---|---|---|---|
| RSA-4096 modulus, 512 B | `.rodata` `0x41cd00` | **`0x8002d2b0`** (`DAT_8002d2b0`) | **yes** — sha256 `84a1c9d842076b759cf7b70150281b812d16d351661836ed77c2af058756ad71` both |
| public exponent | `.data` `0x42f940` = `0x00010001` | `FUN_80019af0`: `*puVar4 = 0x10001` | yes, e = 65537 |
| AES-password RSA blob, 512 B | `.data` `0x42f740` | **`0x80035bd0`** (`DAT_80035bd0`) | **yes** — sha256 `d92ca3839ed433335722f3b5d397e9f05b7ea8925a79862de97689cd3651e6c5` both |
| decrypted passphrase | 64 B, used as EVP_BytesToKey(MD5) pass | same 90-byte PKCS#1 payload → same 64 B | yes |

The loader's own RSA wrapper is `FUN_80019af0(blob, 0x200, out, &outlen)`: it
`malloc(0x204)`, `memcpy`s `DAT_8002d2b0` into `->d`, sets `top = 0x80`,
`dmax = 0x81`, and builds the exponent object with `0x10001` — the same fake
`RSA` structure `decry_firm@0x405490` builds, field for field.

**Consequence:** the AES passphrase was never a secret in either component, and
the loader's copy confirms it is product-static rather than per-image. Anyone
can produce the outer AES layer; nobody can produce the signatures.

## 2. `FUN_80019d08` — the loader's TDE01 verifier

Pseudo-code, de-compiled from the staged ELF (`/tmp/loader-c/mtd1_stage1.elf.c`
lines 15807-15915):

```
FUN_80019d08(in, out_buf, &outlen):
    len = bswap(in[0])                                /* +0x00 = total length */
    RSA_public_decrypt(DAT_80035bd0, 0x200, tmp, &kl) /* -> aes passphrase      */
    RSA_public_decrypt(in + 4, 0x200, digest1, &l1)   /* -> outer signature     */
    sha512(in + 0x204, len - 0x200, h)
    memcmp(h, digest1 + 0x13, l1 - 0x13)              /* OUTER check            */
        else log "Verification Failure! -> encrypted_signed_firmware"
    AES_CBC_decrypt(in + 0x204, len - 0x200, pass, 0x40, 0xa4000000, &olen)
    RSA_public_decrypt(0xa4000000, 0x200, digest2, &l2)
    pad = *(u8 *)(0xa4000000 + olen - 1)
    sha512(0xa4000200, olen - 0x200 - pad, h)
    memcmp(h, digest2 + 0x13, l2 - 0x13)              /* INNER check            */
        else log "Verification Failure! -> firmware"
    memcpy(out_buf, 0xa4000200, olen - 0x200 - pad)
```

That is the same container geometry and the same two `memcmp`s documented for
`decry_firm` (`DECRY-FIRM-FORMAT.md` §2.1-§2.4), including the `+0x13`
DigestInfo skip and the *same* unchecked digest-length arithmetic
(`l - 0x13`). Its debug strings are the loader's own:

```
Verification Failure! -> encrypted_signed_firmware
Verification Failure! -> firmware
Fail to decode signed_digest1
Fail to decode signed_digest2
Fail to decode AES password
```

So the "19-byte PKCS#1 data field makes the compare vacuous" weakness exists in
the loader too — and is equally unreachable, because producing that block needs
the private key.

## 3. `FUN_80001d48` — the burn worker, and where the 64 bytes go

`FUN_80001d48(buf, len, 0)` is called by three paths (AUTOBURN, the vendor
`upgrade_wait` daemon, and `FUN_8001cfcc`). Its first decision:

```
local_90 = buf[0..4]
if (memcmp(&local_90, "boot", 4) != 0) {      /* DAT_80031318 = "boot" */
        log "\ndecryption...\n";
        rc = FUN_80019d08(buf, 0xa6280000, &l);   /* TDE01, §2 */
        if (rc < 0) { log "decryption err\n"; return 0xfffffffa; }
        buf = 0xa6280000; len = l;
        log "decryption success!\n";
}
```

* A file whose first word is `"boot"` is taken as **already plaintext** and
  walked as a record container.
* Anything else (including a genuine TDE01 image, which begins `00 b8 c0 70`,
  and *also* a bare `cr6c` container) is handed to the TDE01 verifier.

After that it walks 16-byte record headers, byte-swapping each field, and
matches the signature against this table (`0x80033f7c`, 8 entries × 0x18):

| # | sig | name string | max size | check |
|---|---|---|---|---|
| 0 | `cs6c` | `Linux kernel` | 0x2c0000 | no |
| 1 | `cr6c` | `Linux kernel (root-fs)` | 0x2c0000 | no |
| 2 | `w6cg` | `Webpages` | 0x20000 | — |
| 3 | `r6cr` | `Root filesystem` | 0x100000 | — |
| 4 | `boot` | `Boot code` | 0x10000 | — |
| 5 | `ALL1` | `Total Image` | 0x200000 | — |
| 6 | `ALL2` | `Total Image (no check)` | 0x200000 | — |
| 7 | `nice` | `squashfs` | 0x100000 | — |

and the uImage magic is a constant in `.data` too (`0x80028f80` holds
`27 05 19 56`, read as the little-endian word `0x56190527`). When the first
record matches it the walker sets

```
local_78 = 0x40;      /* header/record skip */
local_74 = 1;         /* "this container has a uImage header" */
```

**That is the 64-byte skip.** `notes/install-runbook.md` §"What the flasher
does with the 64-byte header" listed this as *not established* and named
`tendaupgrade@0x4018c4` as a suspect. It is established now, and it is the
**loader**, not `tendaupgrade`: the burn worker recognises the Tenda/uImage
64-byte header by its magic and advances the record pointer by `0x40` before it
looks for `cr6c`. This is also why a decrypted vendor image works at all — after
TDE01 decryption the buffer starts with `27 05 19 56`, and the walker steps over
the header to find `cr6c` at +0x40.

Corollary worth stating plainly: **the loader's burn path does not accept a
bare `cr6c` container.** It is not `"boot"`, so it goes to the TDE01 verifier
and comes back `decryption err`. A bare container is written by *userspace*
(`tendaupgrade`, `mtd write`), which is exactly what install path 3 does — and
what the boot path (§4) will then accept.

## 4. Boot path — still no signature

`FUN_80010be4(0xb0040000, ...)` reads the first flash record
(mtd3 is cached at `0xb0040000`):

```
memcmp(word0, "cr6c") == 0  ->  ret 1
memcmp(word0, "cs6c") == 0  ->  ret 2
else log "No sys signature at %X!" -> 0
then: sum of BE u16 over `len` bytes from +0x10 must be 0 mod 2^16
      else log "check failed, ret:%d, sum:%x" -> 0
```

and `FUN_80010e88` uses `ret == 2` (`cs6c`, i.e. "no rootfs in this container")
to additionally validate the rootfs signature at `0xb0040000 + len`. For our
`cr6c` shape (`ret == 1`) **no rootfs check runs and no RSA runs**. This is the
same conclusion `FACTS.md` §3 carries, now with the exact function and strings.

## 5. The loader *does* have an "erase userdata" step

`FUN_80003858` (the vendor `upgrade_wait` path) does, when `_DAT_8003826c != 0`
and the burn returned 0:

```
log "Restore curr config\n"
log "erase restore factory partition......\n"
log "offset:0x%x size:0x%x sector_size:0x%x", 0xfc0000, 0x20000, sector
for (a = 0xfc0000; a < 0xfe0000; a += sector) erase(a)
```

i.e. it erases **CFM (0xfc0000) and CFM_BACKUP (0xfd0000)** — the two RF
calibration partitions — as a "restore factory settings" step. That is the
loader-level equivalent of "erase userdata", and it is the one erase we must
never let happen: RF calibration is read at runtime by our port
(`port/FACTS.md` §5-§6). Any install path that goes through this daemon has to
be treated as destructive to calibration.

## 6. What this changes, and what it does not

**Does not:** open a new path. The loader verifies the same unforgeable
signatures with the same key. There is still no way to make a TDE01 container
that both decrypts and verifies without Tenda's private key.

**Does:**

1. kills the "bootloader might skip the RSA" hypothesis with a function, an
   address and the loader's own log strings;
2. answers the 64-byte-skip question (`local_78 = 0x40` on the uImage magic) and
   moves the credit from `tendaupgrade` to the loader;
3. tells us a bare `cr6c` file is **not** loader-flashable, so `AUTOBURN` with
   `*-factory-cr6c.bin` is the wrong pairing *(UNVERIFIED on silicon — the test
   is: put the file on a TFTP server, `AUTOBURN 1`, `TFTP` it, and watch for
   `decryption err`; if that appears, the loader wants either a TDE01 image or a
   `boot`-led record container, and `mtd write` remains the way in)*;
4. adds a hazard: the `upgrade_wait` branch erases `0xfc0000..0xfe0000`.

## 7. How each claim was checked

| claim | method |
|---|---|
| identical modulus / blob | sha256 of the 512-byte regions at `0x8002d2b0` / `0x80035bd0` vs `decry_firm` `0x41cd00` / `0x42f740` |
| e = 65537 | `FUN_80019af0` stores `0x10001` |
| double signature | `/tmp/loader-c/mtd1_stage1.elf.c` 15811-15915, plus the four log strings |
| record table | 8×0x18 dump at `0x80033f7c`, name pointers resolved to ASCII |
| uImage skip | `FUN_80001d48` at the `FUN_8000ec54(&local_a8, &local_94, 4) == 0` branch → `local_78 = 0x40` |
| boot check | `FUN_80010be4` / `FUN_800109e4`, strings `No sys signature at %X!`, `check failed, ret:%d, sum:%x` |
| CFM erase | `FUN_80003858`, string `erase restore factory partition......`, loop bounds `0xfc0000..0xfe0000` |
| "boot" gate | `FUN_80001d48` first memcmp against `DAT_80031318` = `"boot"` |

Nothing here was run on hardware; every line is static, from this unit's own
`mtd1` dump.
