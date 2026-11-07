# Investigation report

_No brief was recorded for this investigation._

## What we looked at

- US_RX2LProV1.0re_V16.03.51.17_multi_TDE01.bin (unknown)
- US_RX2LProV1.0re_V16.03.51.17_multi_TDE01.dec.bin (unknown)

## What we found

### The local-upgrade gate is an unbreakable signature: bytes 0x04..0x203 are an RSASSA-PKCS1-v1_5 SHA-512 signature over the whole AES payload 0x204..EOF, checked with a 4096-bit public key embedded in decry_firm (.rodata 0x41cd00, e=65537 at .data 0x42f940). Single-byte edits anywhere from 0x1000 to 0xB8C060 are rejected with 'signed2 HASH failed', so no modified or self-built image can pass without Tenda's private key.

- **Confidence:** Seen it (observed)
- **Evidence:**
  - `ev_c3e5916de7c972910c59c834f8e58375ee74c38638e84432aeebd57cd401b8ba` — US_RX2LProV1.0re_V16.03.51.17_multi_TDE01.bin: 0 region(s) (none); 0.0% of 12107892 bytes recognised; 1 unexplained high-entropy span(s).

### The RX2L Pro TDE01 upgrade container is decryptable off-device: the stock /bin/decry_firm, run under qemu-mipsel-static against the extracted rootfs, decrypts the genuine V16.03.51.17 image in place (12107892 to 12106836 bytes). The plaintext is a 0x40-byte Tenda header (payload length big-endian at +0x1c) followed by the cr6c Realtek KernelFS container, laid out exactly like the device's own mtd3.

- **Confidence:** Seen it (observed)
- **Evidence:**
  - `ev_f1502249719ece2d0a02182309257f638dd8c2fe9f31320434c765e8f41c71de` — Wrote 5 file(s), 49697788 bytes, from US_RX2LProV1.0re_V16.03.51.17_multi_TDE01.dec.bin into /root/rx2lpro/work/extract51 (3 carved, 1 decompressed, 1 payload); 2 level(s) deep. (path=/root/rx2lpro/work/extract51 (output folder))

### The AES key is product-static, not per-image: the RSA plaintext holds only a SHA-512 DigestInfo and no key material, while decry_firm's openssl_aes_password unwraps an AES password from a block embedded in its own .data (0x42f740) and drives EVP_BytesToKey(MD5) AES-CBC over the payload. Decryption therefore works for every image of this generation while signing cannot be reproduced.

- **Confidence:** Worked it out (derived)
- **Evidence:**
  - `ev_c3e5916de7c972910c59c834f8e58375ee74c38638e84432aeebd57cd401b8ba` — US_RX2LProV1.0re_V16.03.51.17_multi_TDE01.bin: 0 region(s) (none); 0.0% of 12107892 bytes recognised; 1 unexplained high-entropy span(s).
  - `ev_f1502249719ece2d0a02182309257f638dd8c2fe9f31320434c765e8f41c71de` — Wrote 5 file(s), 49697788 bytes, from US_RX2LProV1.0re_V16.03.51.17_multi_TDE01.dec.bin into /root/rx2lpro/work/extract51 (3 carved, 1 decompressed, 1 payload); 2 level(s) deep. (path=/root/rx2lpro/work/extract51 (output folder))

### Installation must bypass the signature stage instead of satisfying it: lib/libcommonprod.so shows the working contract is 'tendaupgrade <fwtype> <1|0> /var/image' and bin/tendaupgrade only touches /dev/mtdN plus KernelFS1/KernelFS2 and env image_boot, so a self-built cr6c KernelFS staged in RAM and written by the vendor's own /bin/tendaupgrade is the viable route; /proc/km/km_upgrade_ignore_kernel_sig exists on the device but is mode 0440 and reads 0, so it is not a usable bypass.

- **Confidence:** Seen it (observed)
- **Evidence:**
  - `ev_f1502249719ece2d0a02182309257f638dd8c2fe9f31320434c765e8f41c71de` — Wrote 5 file(s), 49697788 bytes, from US_RX2LProV1.0re_V16.03.51.17_multi_TDE01.dec.bin into /root/rx2lpro/work/extract51 (3 carved, 1 decompressed, 1 payload); 2 level(s) deep. (path=/root/rx2lpro/work/extract51 (output folder))

## Open questions

- What passphrase does the on-device upgrade script feed to OpenSSL for the TDE01 container? _(severity High, Open)_
  - would settle it: 42,264 KDF candidates tried: 7,044 passphrases x {md5,sha256} x {16,24,32-byte keys}, validated on gzip magic 1f8b08 at the first plaintext block - no hit
  - would settle it: 6 apparent hits were statistical noise (expected 2-3 two-byte collisions per 150k trials)
  - would settle it: 512-byte header is statistically indistinguishable from random: entropy 7.620 vs 7.65 expected for 512 uniform bytes; differs between sibling TDE01 images (~13/1024 bytes coincide) - so it is a signature or a wrapped per-build key, not a passphrase oracle

## How this was done

- **Mode:** Deep (the original's code and files could be read)
- **Engines:** builtin
- **Limitations:**
  - Carved as raw bytes but not unpacked (the built-in engine has no reader for their contents): elf ×1.
  - Content inside a region of confirmed length (for example the compressed kernel inside a boot image) is not listed here; firmware_image_extract unpacks and scans it.
  - Entropy classes (text, code-or-data, compressed-or-encrypted, padding) are statistical hints, not proof of content.
  - Regions are found by known signatures checked against their headers; formats we don't know, encrypted or vendor-specific containers are not recognised, and absence of a region is not proof.
  - Regions whose length the format doesn't state are carved up to the next region or the end of the data.
  - the kind is detected from the first bytes and, to break ties, the file name

