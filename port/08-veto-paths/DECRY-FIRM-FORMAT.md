# `decry_firm` — exact container format and verification (Tenda RX2L Pro, MIPS32 LE)

Binary analysed: `/tmp/rxsys/bin/decry_firm` (134,284 bytes, static uClibc + static
OpenSSL, SHA-256 `d69d4ee2b63556e48b0b4b430a46f35ecb1fdb3a6e3589b26ead01c65ebfeb03`).
All addresses are virtual addresses in that ELF (`.text` = 0x400760..0x41ca20).
Ground-truth image used for the positive control: a genuine
`US_RX2LProV1.0re_V16.03.51.17_multi_TDE01.bin`-generation container
(12,107,892 bytes, SHA-256 `f2ec09dc39f308faa1218c1d8a9bab637cfffb1d83922f96597bcdb41e8a9020`)
that decrypts to 12,106,836 bytes, SHA-256
`d1ee92c0339dfaa451aaf4ae0f32638e1a097a50fb11f31ce32d5b3ae409bc70`.

---

## Readable form

The same functions decompiled to C by Ghidra 12.1.4 are in
[`ghidra/decry-firm.c`](ghidra/decry-firm.c). What they show, in the source's own
shape:

```c
/* main */
if (argv[2] && access(argv[2], 0) == 0) unlink(argv[2]);      /* clear stale flag */
rc = decrypt_firmware(argv[1]);
if (rc == 0) return 0;
puts("openssl decode failed");
if (argv[2]) { fd = creat(argv[2], 0777); if (fd >= 0) close(fd); }   /* record it */
return -1;

/* decrypt_firmware */
fstat(fd, &st);  if (!S_ISREG(st.st_mode)) -> "get ori stat failed!"
map = mmap(0, st.st_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
pass = get_aes_pass();                                   /* RSA-public-decrypt .data */
sig2 = decrypt_signed_digest(map + 0x004, &len2);        /* OUTER signature block  */
if (verify(map + 0x204, size - 0x204, sig2, len2))       /* over the ciphertext    */
    -> "signed2 HASH failed!"
aes_cbc_decrypt(map + 0x204, size - 0x204, pass, 0x40, map, &len);   /* in place   */
sig1 = decrypt_signed_digest(map, &len1);                /* INNER signature block  */
pad  = map[len - 1];
if (verify(map + 0x200, (len - 0x200) - pad, sig1, len1))  /* over the payload     */
    -> "signed2 HASH failed!"
memcpy(map, map + 0x200, len - 0x200);  ftruncate(fd, len);          /* strip hdr  */
return 0;
```

Two things this settles beyond the disassembly: the `S_ISREG` test is
`fstat`/`0x8000` (not a header field, as one draft claimed), and the **inner**
signature is the first 0x200 bytes of the decrypted stream while the **outer** one
sits at file offset 0x004.

## 0. Verdict up front

**NO for attacker-chosen content. A hand-built file cannot make `decry_firm`
return 0 and emit a payload of the attacker's choosing unless it carries a
genuine Tenda RSA-4096 signature (or is a genuine image copied verbatim, apart
from the ignored 4-byte header — see §3, case `hdr`).**

The gate is a real verification, not a log: **RSASSA-PKCS1-v1_5 with SHA-512**,
performed **twice**:

1. **outer** — signature in bytes `0x004..0x203` over `SHA512(bytes 0x204..EOF)`
   (the whole AES blob), checked at `0x4064e8` → `0x4061fc` → `memcmp` @ `0x406270`;
2. **inner** — a second 512-byte signature that is the first 0x200 bytes of the
   *decrypted* stream, over `SHA512(payload)`, checked at `0x4065fc`.

The AES passphrase is **not** a secret — it is recovered at run time by RSA
*public* decryption of a 512-byte blob embedded in the binary itself (§2.3).
Anyone can therefore build the AES layer. Only the two RSA signature blocks
stop a forgery, and producing either one requires Tenda's private key.

The only weakness in the code is that the **digest length is never pinned to
0x40**: `0x4061fc` compares `rsa_data_len − 0x13` bytes. If an RSA block with
exactly `0x13` bytes of PKCS#1 data could be produced, the `memcmp` would be
zero-length and would pass. Producing *any* PKCS#1 v1.5 block requires the
private key (the modulus is a 4096-bit composite, not prime, no factor < 10^6),
so the weakness is **theoretical, not reachable**. Details in §2.6.

The 4-byte word at offset 0 is **never read** by `decry_firm` (proven: changing
it to `DE AD BE EF` still exits 0 and produces the identical plaintext). It is
the only unauthenticated field of the container.

---

## 1. Container layout (as accepted by `decry_firm`)

Genuine 12,107,892-byte file; `S = st_size` (`fstat`, offset `0x74(fp)`);
`N = 0x41cd00` blob (RSA modulus), size 512 bytes.

| offset | size | field | endianness / handling |
|---|---|---|---|
| `0x000` | 4 | total length field. In genuine images `u32be == filesize − 4` (e.g. `00 B8 C0 70` = 12,107,888). | **never read by `decry_firm`** — changing it does not affect `rc` or output (test `hdr`). |
| `0x004` | 512 | **RSA signature #1** (outer), PKCS#1 v1.5 block, raw 4096-bit ciphertext. | big-endian integer `c1`; `c1^e mod N` must be `00 01 FF…FF 00 ‖ DigestInfo`. |
| `0x204` | 8 | ASCII `"Salted__"` | checked with `memcmp` @ `0x405dc4` against `0x41cfc0`; mismatch → `-1` (prints `valid salt` only when debug is on). |
| `0x20C` | 8 | salt | used by EVP_BytesToKey(MD5). |
| `0x214` | `S-0x214` | **AES-128-CBC ciphertext** of the inner plaintext. | key = `MD5(pass‖salt)`, IV = `MD5(key‖pass‖salt)`; `pass` = first 64 bytes of the RSA plaintext of the `.data` blob @ `0x42f740` (§2.3). |

Inner plaintext (produced by AES decryption, length `L = S − 0x214`):

| inner offset | size | field |
|---|---|---|
| `0x000` | 512 | **RSA signature #2** (inner), PKCS#1 v1.5 over `SHA512(payload)`, big-endian. |
| `0x200` | `L − 0x200 − pad` | payload (becomes the output file verbatim). |
| `L − pad` | `pad` | padding; `pad` = last decrypted byte (genuine images use PKCS#7-style 0x0C…0x0C). **Only the last byte is read; the other padding bytes are never validated** (`0x4065c4`–`0x4065e0`). |

Output: `memmove(base, base+0x200, payload_len)` @ `0x406678`, then
`ftruncate(fd, payload_len)` @ `0x40668c`. The output is exactly the bytes that
were hashed for signature #2, so a successful run can only ever emit a
vendor-signed payload.

---

## 2. What is actually verified

### 2.1 The RSA primitive — `0x400b04` (a rewrite of `RSA_public_decrypt`)

`0x405490(in, in_len, out, &out_len)` builds a fake RSA object and calls
`0x400b04`:

* `0x40550c` `malloc(0x204)`; `0x405574` `memcpy(obj->d, 0x41cd00, 0x200)`;
  `0x405594…0x4055a8` set `top=0x80` (128 limbs = 4096 bits), `dmax=0x81`,
  `neg=0`, `flags=1`. So **N = the 0x41cd00 blob interpreted as 128
  little-endian 32-bit limbs**.
* `0x4055c0` `malloc(4)`; `0x40560c` stores the global at **`0x42f940` =
  `0x00010001`** → **e = 65537**, `top=dmax=1`.
* `0x405690` pushes **padding = 1** (`RSA_PKCS1_PADDING`) as the 5th argument.

Inside `0x400b04`:

| addr | action |
|---|---|
| `0x400b38`–`0x400b50` | `BN_num_bits(N) < 0x4001` (16385) |
| `0x400b64`–`0x400b84` | `BN_cmp(N, e) > 0` |
| `0x400b98`–`0x400bdc` | reject only if `BN_num_bits(N) ≥ 0xC01` **and** `BN_num_bits(e) ≥ 0x41` (this odd guard passes our key: 4096-bit N, 17-bit e) |
| `0x400ca0` | `BN_bin2bn(ciphertext, 0x200, bn)` — **big-endian** input |
| `0x400cbc` | `BN_cmp(bn, N) < 0` (rejects c ≥ N) |
| `0x400d54` | `BN_mod_exp_mont(rr, c, e, N, ctx, mont)` → `m = c^e mod N` |
| `0x400db4` | `BN_bn2bin(rr, buf)` → `flen` (leading zero byte is dropped) |
| `0x400e18` | `0x400970(out, 512, buf, flen, 512)` — PKCS#1 type-1 unpad |

`0x400970` (`RSA_padding_check_PKCS1_type_1`):

* `0x4009a0` requires `num == flen + 1` (i.e. 512 == flen+1 ⇒ the dropped byte
  was `0x00`, so `m` is a 4088-bit number);
* `0x4009c0` requires first emitted byte `== 0x01`;
* `0x4009ec` scans `0xFF`; `0x400a7c` requires **≥ 8** `0xFF` bytes;
* `0x400a5c`/`0x400a78` require a `0x00` terminator before the last byte;
* `0x400ae0` copies the remaining data and returns its length `j` (1..501).

Any failure returns `-1`; `0x405490` then returns `-1` and
`decrypt_signed_digest` returns `NULL` → `"[decrypt_signed_digest][36] get
signed_digest failed!"` and `decrypt_firmware` aborts.

### 2.2 `decrypt_signed_digest` — `0x406080`

```
0x4060d0  rc = 0x405490(in, 0x200, stackbuf[0x600], &len)
0x4060d8  if (rc) return NULL                       // "get signed_digest failed!" line 36
0x406114  len = *(a1)
0x406124  if (len == 0) return NULL                 // "signed_digest len failed!" line 43
0x40616c  p = malloc(len+1)                         // "signed_digest malloc failed!" line 49
0x4061c8  memcpy(p, stackbuf, len); p[len] = 0
          return p                                  // caller frees it
```

### 2.3 AES passphrase — `0x405b8c`, called first from `0x406414`

`0x405b8c(&fp+0xdc)` is a clone of `decrypt_signed_digest` that **RSA-public-decrypts
the fixed 512-byte blob at `0x42f740`** (`0x405bd4` `lui $v0,0x43; addiu
$a0,$v0,-0x8c0` ⇒ `0x430000−0x8C0 = 0x42f740`), length `0x200`. It returns the
NUL-terminated plaintext (or `NULL` → `"[decrypt_firmware][111] get aes_pass
failed!"`).

With `N`/`e` above, `RSA_public_decrypt(0x42f740)` is a valid PKCS#1 block with
90 data bytes:

```
TZVzOlV0tJnQ5mkSJu9jVx4IN62DE1mT1blmfYteqZ9LFNeIfZzrNmhSBVmeHQHS\n
t/GU20StkNunubF4Xb8ctQ==\n
```

The code uses **the first 0x40 (64) bytes** of that string as the OpenSSL
passphrase (`0x405d08` arg3 = 0x40):

```
pass = "TZVzOlV0tJnQ5mkSJu9jVx4IN62DE1mT1blmfYteqZ9LFNeIfZzrNmhSBVmeHQHS"
key  = MD5(pass ‖ salt)
iv   = MD5(key ‖ pass ‖ salt)          // exactly EVP_BytesToKey(MD5, 1 iter), AES-128-CBC
```

`0x4079ec` = `MD5_Init`, `0x407790` = `MD5_Update`, `0x40759c` = `MD5_Final`
(sequence at `0x405e14`–`0x405ef4`: update(pass,0x40), update(salt,8), final →
D1; init, update(D1,16), update(pass,0x40), update(salt,8), final → D2).
`0x409598` = `AES_set_decrypt_key(D1, 128, ctx)`; `0x409648` =
`AES_cbc_encrypt`-style block loop; ciphertext is fed from `base+0x204+0x10`
(`0x405f74` starts the offset at 0x10), plaintext is written at `base+0`
(`0x406010` `a1 = out + *outlen`).

Re-encrypting the genuine plaintext with this key/IV reproduces the genuine
ciphertext byte-for-byte (verified), so the AES layer is fully forgeable and
offers no security.

### 2.4 The two signature checks — `0x4061fc`

`0x4061fc(in, in_len, sig_plain, sig_len)`:

```
0x406230  memset(digest, 0, 0x41)
0x406248  0x405a00(in, in_len, digest)        // SHA-512(in, in_len)
0x406270  return memcmp(digest, sig_plain + 0x13, sig_len - 0x13)
```

`0x405a00` is a SHA-512 wrapper (`0x402134` SHA512_Init, `0x402278`
SHA512_Update, `0x402504` SHA512_Final); its debug dump (`"SHA512:"`, `%02X`,
0x405a20–0x405b30) is gated on `0x42f950 & 4` and a log pointer at
`0x42f9c0`, **both of which are never written anywhere in the binary** (33
references, 0 stores) — all debug prints are dead code. The real comparison is
the `memcmp` above, and its return value is used.

**Outer check** (`decrypt_firmware`, `0x406464`–`0x4064f0`):

```
0x406464  0x406080(mmap + 4, &len1)              // RSA-decrypt signature #1
0x4064e8  0x4061fc(mmap + 0x204, S - 0x204, sig1, len1)
0x4064f0  if (rc != 0) → "[decrypt_firmware][124] signed2 HASH failed!" and abort
          // rc is memcmp(SHA512(ciphertext 0x204..EOF), sig1+0x13, len1-0x13)
```

**Inner check** (`0x406560`–`0x406604`):

```
0x406560  0x405d08(base+0x204, S-0x204, pass, 0x40, out=base, &outlen)   // AES
0x406574  0x406080(base, &len2)                  // RSA-decrypt signature #2
0x4065c4  payload_len = outlen - 0x200 - base[outlen-1]   // only last byte read
0x4065fc  0x4061fc(base + 0x200, payload_len, sig2, len2)
0x406604  if (rc != 0) → "[decrypt_firmware][140] signed2 HASH failed!" and abort
```

### 2.5 Verification table (address → check → failure)

| address | check | failure |
|---|---|---|
| `0x4062dc` | `argv[1] != NULL` | `"[decrypt_firmware][88] path is NULL!"` |
| `0x40631c` | `open(path, 0x102)` = `O_RDWR\|O_CREAT` | `"[decrypt_firmware][95] open ori failed!"` |
| `0x406370` | `fstat` | `"[decrypt_firmware][100] get ori stat failed!"` |
| `0x40638c` | **`st_mode & 0xf000 == 0x8000`** (S_IFREG; `fstat` result at `fp+0x40`, `st_mode` at `+0x14`) | same error |
| `0x406408` | `mmap(NULL, st_size, 3, 1, fd, 0)` — return value **not checked** | later SIGSEGV |
| `0x406414` | `0x405b8c` → RSA-public-decrypt `0x42f740` → AES passphrase | `"get aes_pass failed!"` line 111 |
| `0x406464` | RSA-public-decrypt `mmap[4..0x204]` with N=`0x41cd00`, e=65537, PKCS#1 v1.5 | `"[decrypt_signed_digest][36] get signed_digest failed!"` + `"[decrypt_firmware][118] get signed2 failed!"` |
| `0x4064f0` | `memcmp(SHA512(mmap[0x204..S]), sig1+0x13, len1-0x13)` | `"[decrypt_firmware][124] signed2 HASH failed!"` |
| `0x405dc4` | first 8 bytes of AES region == `"Salted__"` | return −1 from `0x405d08` (silent unless debug) |
| `0x40658c` | RSA-public-decrypt `base[0..0x200]` (signature #2) | `"[decrypt_signed_digest][36] …"` + `"[decrypt_firmware][134] get signed1 failed!"` |
| `0x406604` | `memcmp(SHA512(base[0x200..]), sig2+0x13, len2-0x13)` | `"[decrypt_firmware][140] signed2 HASH failed!"` |
| `0x406678` | `memmove(base, base+0x200, payload_len)`; `ftruncate` to same | — (only after both checks) |
| `0x406694`/`0x40670c` | return 0 | — |

`main` (`0x406728`) returns 0 on success; on failure it `puts("openssl decode
failed")`, `creat(argv[2], 0777)` (→ mode 0700 after umask) and returns −1
(exit status 255).

### 2.6 The one real weakness: unchecked digest length (not exploitable)

`0x4061fc` takes the comparison length from the RSA plaintext:
`len = rsa_data_len − 0x13` (0x13 = 19-byte SHA-512 `DigestInfo` prefix
`30 51 30 0d 06 09 60 86 48 01 65 03 04 02 03 05 00 04 40`). Nothing checks
that `rsa_data_len == 0x53`, and nothing checks the first 0x13 bytes of the
signature plaintext.

Consequences:

* `rsa_data_len == 0x13` ⇒ `memcmp(..., 0)` ⇒ **returns 0 ⇒ signature check
  bypassed**; the same applies to the inner check.
* `rsa_data_len < 0x13` ⇒ length underflows to ~4 GiB; the `memcmp`/`SHA-512`
  reads out of bounds (crash/DoS), it does not pass.
* `rsa_data_len > 0x13` ⇒ more digest bytes are compared; still requires a
  matching signature.

To exploit the zero-length case one must supply a PKCS#1 v1.5 block of the form
`00 01 FF…FF 00 ‖ 19 bytes`, i.e. compute `m^d mod N` for a chosen `m`. That is
RSA private-key inversion. `N` is a genuine 4096-bit composite (Miller-Rabin:
composite; `gcd(N,e)=1`; not a perfect square; no factor < 10^6), so this is
not feasible. The only 512-byte RSA blocks that pass are genuine Tenda
signatures.

The DigestInfo prefix is also not validated, but that does not help: the 64-byte
SHA-512 digest itself is compared, and it sits at `+0x13` regardless of the
prefix.

---

## 3. `qemu` experiments (all run under `qemu-mipsel-static -L /tmp/rxsys`)

Command: `qemu-mipsel-static -L /tmp/rxsys /tmp/rxsys/bin/decry_firm INPUT.bin FLAG`.

| # | input | size | rc | FLAG | input changed | first stderr/stdout |
|---|---|---|---|---|---|---|
| 1 | `valid.bin` = genuine RX2LPro 51.17 container | 12,107,892 | **0** | no | yes → 12,106,836, sha256 `d1ee92c0…` | (silent) |
| 2 | `hdr.bin` = #1 with bytes 0–3 = `DE AD BE EF` | 12,107,892 | **0** | no | yes → identical output `d1ee92c0…` | (silent) — **header is ignored** |
| 3 | `zero.bin` = 0x400 zero bytes | 1,024 | 255 | yes | no | `[decrypt_signed_digest][36] get signed_digest failed!` / `[decrypt_firmware][118] get signed2 failed!` / `openssl decode failed` |
| 4 | `craft.bin` = `u32be len` + 0x200 zeros + `"Salted__"`+8 salt+0x400 zeros | 1,556 | 255 | yes | no | same as #3 |
| 5 | `flip_ci.bin` = #1 with ciphertext byte `0x5000` XOR 0xFF (sig #1 intact) | 12,107,892 | 255 | yes | no | `[decrypt_firmware][124] signed2 HASH failed!` |
| 6 | `flip_sig.bin` = #1 with signature byte `0x4` XOR 0xFF | 12,107,892 | 255 | yes | no | `[decrypt_signed_digest][36] get signed_digest failed!` |
| 7 | `reenc_mod.bin` = own AES re-encryption (known key/IV, genuine salt) of genuine plaintext with 1 payload byte flipped, genuine sig #1 | 12,107,892 | 255 | yes | no | `[decrypt_firmware][124] signed2 HASH failed!` |
| 8 | `reenc_newsalt.bin` = own AES re-encryption of genuine plaintext under a new salt, genuine sig #1 | 12,107,892 | 255 | yes | no | `[decrypt_firmware][124] signed2 HASH failed!` |
| 9 | `justsig.bin` = #1 truncated to 0x204 (sig only, empty payload) | 516 | 255 | yes | no | `[decrypt_firmware][124] signed2 HASH failed!` (SHA-512("") ≠ genuine digest) |
| 10 | `short.bin` = 0x100 bytes | 256 | 255 | yes | no | `[decrypt_signed_digest][36] get signed_digest failed!` |
| 11 | `empty.bin` = 0 bytes | 0 | 139 | no | no | qemu: uncaught target signal 11 (mmap(0) fails, return unchecked) |
| 12 | nonexistent path | — | 139 | no | created 0-byte file | mmap(0) → SIGSEGV |
| 13 | `/dev/null` (char device) | — | 255 | yes | no | `[decrypt_firmware][100] get ori stat failed!` — proves the `0x40638c` test is `S_ISREG(st_mode)`, not a header field |

Positive control notes: #1 and #2 produce the same output, sha256
`d1ee92c0339dfaa451aaf4ae0f32638e1a097a50fb11f31ce32d5b3ae409bc70`, which is the
known RX2LPro 51.17 plaintext. #1 took ≈3.5 s under qemu. Every failing regular-
file case that reaches `main`'s error path creates a 0-byte mode-0700 flag at
`argv[2]`; the two SIGSEGV cases do not.

Additionally verified off-line with Python/cryptography:

* `SHA512(c[0x204:]) == sig1_digest` for the genuine container (outer check);
* `sig2 = AES_decrypt(c)[0:0x200]` decrypts to a valid PKCS#1 block whose digest
  equals `SHA512(payload)` (inner check);
* `AES_encrypt(genuine_plaintext, key, iv) == c[0x214:]` byte-for-byte, using
  `pass` from §2.3 and `salt = c[0x20C:0x214]`;
* `0x42f740` RSA-public-decrypts with `N`/`e` to the 90-byte passphrase blob
  (PKCS#1: `00 01 FF×419 00 ‖ 90 bytes`).

Reference values for the embedded key:

```
N  = 512 bytes at .rodata 0x41cd00, read as 128 little-endian 32-bit limbs,
     4096 bits, top32 = 0xbaec6121, low32 = 0x4dfef60b,
     sha256(blob) = 84a1c9d842076b759cf7b70150281b812d16d351661836ed77c2af058756ad71
e  = 0x00010001 (65537), u32 at .data 0x42f940
pw = 512-byte blob at .data 0x42f740 (sha256 d92ca3839ed433335722f3b5d397e9f05b7ea8925a79862de97689cd3651e6c5)
```

---

## 4. Verdict

`decry_firm` accepts a very small family of files: a 4-byte big-endian length
header (ignored by the tool), a 512-byte RSA-4096 PKCS#1 v1.5 signature over
`SHA-512` of everything from offset `0x204` to EOF, then an OpenSSL
`"Salted__"`+salt+AES-128-CBC blob whose passphrase is itself recovered by
RSA-public-decrypting a blob embedded in the binary. The AES layer is therefore
public and can be reproduced by anyone (proven byte-for-byte), but the ciphertext
is covered by the outer RSA signature, and the decrypted payload is covered by a
second RSA signature embedded in its first 0x200 bytes. Both signatures are
checked with a real `memcmp` (`0x406270`, result consumed at `0x4064f0` and
`0x406604`); the debug printers are dead code. The only flaw is the unchecked
digest length (`memcmp` length = `rsa_data_len − 0x13`, so `rsa_data_len == 0x13`
would make the check vacuous), but reaching it requires a PKCS#1 v1.5 block with
19 data bytes, i.e. an RSA private-key operation against a 4096-bit composite
modulus with no small factors — out of reach. **So: no crafted container can
make `decry_firm` exit 0 with attacker-controlled output without a genuine
Tenda signature; the one check that stops it is the `memcmp` at `0x406270`
(outer, over the AES ciphertext) and its twin at `0x406604` (inner, over the
decrypted payload).** The only variation a hand-built file may carry is the
4-byte header at offset 0, which is never read and cannot change the output.
