# Artifact audit: every dump, cross-checked against the port's claims

Source artifacts (all from this unit; dumps made over the vendor webui/telnet,
nothing written to flash):

| artifact | size | sha256 (first 16) |
|---|---|---|
| `flash/mtd1_Bootloader.bin` | 196,608 (0x30000) | `18fa9ebebbed9e9d…` |
| `flash/mtd2_CFG.bin` | 65,536 | `5d5794557cb2a6ce…` |
| `flash/mtd3_KernelFS.bin` | 16,252,928 (0xF80000) | `7e2d13245d3f7450…` |
| `flash/mtd5_CFM.bin` | 65,536 | `4d6065246df3e7ec…` |
| `flash/mtd7_MISC2.bin` | 65,536 | `ec8cba3847aba59d…` |
| `dec/RX2LPro_51.17_decrypted.bin` | 12,106,836 | (whole decrypted upgrade image) |

Per-unit values (MACs, WPS PIN, serial) are deliberately **not** reproduced in
this file; where an offset is given, the bytes live on the device only.

---

## 1. Partition map — three sources, one of which is wrong

`/proc/mtd` from the device (authoritative):

```
mtd0 ALL        0x00ff0000
mtd1 Bootloader 0x00030000  @0x00000000
mtd2 CFG        0x00010000  @0x00030000
mtd3 KernelFS   0x00f80000  @0x00040000
mtd4 RootFS     0x00bbf7ee  @0x00400812   (carved: 0x40000 + 16 + len)
mtd5 CFM        0x00010000  @0x00fc0000
mtd6 CFM_BACKUP 0x00010000  @0x00fd0000
mtd7 MISC2      0x00010000  @0x00fe0000
```

Arithmetic check: `0x400812 + 0xBBF7EE = 0xFC0000` = mtd5 ✔, and mtd5/6/7 tile
the last 192 KiB ✔.

**The sysfs capture in the same session printed mtd4 size = 12318702**, which
overruns mtd5 by 512 bytes. The two cannot both be right; `/proc/mtd` agrees
with the partition arithmetic and with mtd5's offset, so `port/FACTS.md` has been
corrected from 12318702 to 12318190 and this note records why.

Also confirmed against the device's own cmdline
(`pulled/info.txt`): `console=ttyS0,115200 root=/dev/mtdblock4` — 115200, as the
DTS says, and the stock root device is the carved RootFS.

## 2. mtd1 — what the bootloader dump actually is

The raw partition is **not** U-Boot text: it is the Realtek bootcode plus the
environment, and the stage that validates images is **gzip-compressed inside
it**:

```
0x00000..   bootcode ("init_ram", "init ddr ok", "DRAM Type: DDR2",
            "SPI image move done ->Jump to DRAM", "v3.4.13")
0x08d60..   gzip stream (1f 8b 08 00 00 00 00 00 02 03) -> 229,584 bytes
0x2f004..   environment defaults: disaster_web=1, ipaddr=192.168.1.1,
            netmask=255.255.255.0, serverip=192.168.1.99
```

A raw string search of the partition finds **none** of the strings this repo's
loader analysis quotes — which looked like a contradiction until the gzip layer
was accounted for. Decompressing the member at 0x8d60 yields all of them:

| string | offset in the decompressed stage |
|---|---|
| `Jump to image` | 0x32abc |
| `cr6c` / `cs6c` | 0x311a0 / 0x311a8 |
| `No sys signature` | 0x32b58 |
| `check failed` | 0x32b74 |
| `sum:` | 0x32b8a |

So the loader contract recorded in `port/FACTS.md` §3 is backed by this unit's
own bootloader, and the earlier analysis was reading the decompressed stage.

**The strongest confirmation is the flash itself**: mtd3's container satisfies
the BE-u16 zero-sum over `len` bytes from `+0x10` **exactly** (sum == 0,
verified on the dump), and this device boots. The contract is not a theory.

## 3. mtd3 — the flashed container vs the decrypted upgrade image

| field | flash (mtd3) | decrypted upgrade image |
|---|---|---|
| magic @0 | `cr6c` | `cr6c` (at +0x40, behind the 64-byte Tenda header) |
| start_addr | 0x80D00000 | 0x80D00000 |
| burn_addr | 0x00040000 | 0x00040000 |
| len | 0x3C0802 | 0x3C0C02 (**0x400 larger**) |
| squashfs at 16+len | `nice` @0x3C0812 | `nice` @0x3C0C12 |

Same loader contract, different build — the OTA image is 1 KiB larger. Both put
the squashfs exactly at `16 + len`, which is the invariant `mtdsplit_cvimg`
depends on. The squashfs magic is the vendor-mangled `nice`, not `hsqs`: our
kernel must therefore use the vendor's SQUASHFS_MAGIC patch (it does — that is
why the build's container gate refuses a mangled magic only with
`--allow-mangled`).

## 4. mtd2 CFG — every key this port relies on, with its real offset

| key | key offset | value offset | what the port does with it |
|---|---|---|---|
| `product.id` | 0x00058f | 0x000598 | must equal the image's `+0x24`; read as `0x00031053` ✔ |
| `lan_mac` | 0x000233 | **0x00023b** | DTS `mac-base` cell `reg = <0x23b 0x11>` ✔ |
| `wlan0_mac` | 0x0005ab | **0x0005b5** | DTS cell `reg = <0x05b5 0x11>` ✔ |
| `wlan1_mac` | 0x0005cd | **0x0005d7** | DTS cell `reg = <0x05d7 0x11>` ✔ |
| `lanvlan` / `wanvlan1` | — | — | `1 0x15d 0x40` / `2 0x142 0x40` → LAN ports {0,2,3,4}, WAN {1}, CPU {6} |
| `lan1/2/3.phyid` | — | — | 4 / 3 / 2 → the jack→port map used by `02_network` |
| `wan1.phyid_default` | — | — | 1 → WAN jack on switch port 1 |

There is **no `wan_mac` key** — confirming that the WAN MAC is derived, and our
`LAN + 1` rule is a choice, not a recovery.

## 5. The partitions the port must never write

* **mtd5 CFM** and **mtd6 CFM_BACKUP** are the vendor's running config
  (1,209 printable key/value strings). They **differ in 645 bytes** — the
  "backup" is not a copy, so treating either as disposable would lose settings
  the other does not hold.
* **mtd7 MISC2** is a small JSON blob (a MAC-prefix vendor table, a randomised
  MAC list and an interval) — vendor device-recognition state, not calibration.
* Neither CFM nor MISC2 carries RF calibration; that lives in
  `/etc_ro/wlan/rtl8832bre/RFE50/**` on the rootfs, which the port reads at
  runtime and which is unchanged by a KernelFS write.

## 6. What this audit proves, and what it does not

Proven from these artifacts: the flash geometry, the container contract (magic,
addresses, zero-sum, `16+len` squashfs), the CFG key offsets the DTS binds, the
absence of a `wan_mac`, the loader's own error strings and env defaults, and that
CFM/CFM_BACKUP are not interchangeable.

Not proven by them: anything about how *our* image behaves — that needs the
device. In particular the RF calibration path and the 5 GHz card remain
untested.

## 7. mtd4 / the vendor rootfs: valid squashfs, not readable by stock tools

Unmangling the 4-byte magic (`nice` → `hsqs`, a copy only) gives a parseable
superblock from **both** copies:

| | flashed (mtd4) | OTA image (decrypted) |
|---|---|---|
| squashfs version | 4.0 | 4.0 |
| filesystem size | 8,172,452 | 8,170,944 |
| block size / compression | 131072 / xz | 131072 / xz |
| mkfs_time | 2038-06-11 (bogus) | 2038-05-30 (bogus) |

They are **not the same build** (1,508 bytes apart), which matches the container
`len` difference of 0x400 noted in §3.

But neither extracts: stock `unsquashfs` reads the superblock, then dies with

```
read_block: failed to read block @0x7ca0xx
read_fragment_table: failed to read fragment table index
FATAL ERROR: File system corruption detected
```

— at the *same* relative position in both. So the vendor's mksquashfs output is
non-standard beyond the magic; the fragment table does not decode. Consequences:

* do not plan on unpacking or repacking the stock rootfs with standard tools; the
  userspace analysis this port relies on came from an extraction that predates
  this audit, not from these attempts;
* it is another reason our own images ship a **plain `hsqs`** (and why the build
  gate rejects a mangled magic in our containers unless told it is a stock dump):
  we control the whole pipeline on our side, and the vendor's format is a trap.

Recorded as an observation, not a blocker: nothing in the port reads mtd4.

## 8. The per-unit calibration data, validated against the driver's own conditions

The apmib records in mtd2 are not opaque blobs; two of them can be checked against
the code that consumes them.

| key | value | check |
|---|---|---|
| `HW_WLAN1_11N_XCAP` | **45** | the driver latches xcap only when `0 < xcap < 0x3F` (`8192cd_hw.c:8802`) and programs BB `0x2C` bits 12-23 with it. 45 is inside the window, so this unit's crystal trim will be applied, not ignored. |
| `HW_WLAN1_TX_POWER_CCK_A` | 28 hex chars | 14 bytes = 14 channels — the 2.4 GHz rate table, exactly the length `rfhw-convert` expects. A 13- or 15-entry table would have been silently truncated or padded. |
| `HW_WLAN1_TX_POWER_HT40_1S_A` | 28 hex chars | same, HT40 1-stream table |
| `HW_WLAN1_WLAN_ADDR` | 12 hex chars | the per-unit 2.4 GHz MAC the driver would otherwise replace with `rtk_fake_addr` |
| `HW_XCAP_WLAN0_AX` | 53 | the 5 GHz/AX trim — present, but nothing in this port applies it (no 8832 driver) |

So the calibration store is well-formed for group 1 and the values are in range;
applying them (the `rfhw-convert` path added this round) is the difference between
a calibrated radio and one running on the driver's built-in tables.

Values themselves are per-unit and are not reproduced here beyond the two that a
range check needs.

## 9. `image_boot` is dead weight — nothing reads it

`tendaupgrade` runs `env set image_boot 1 unlock` after a successful flash
(found in its strings at 0x4037c0). That looked like an A/B bookkeeping step that
a flashing procedure must respect. It is not: the only artifacts in this firmware
that mention the key at all are

* `bin/env` — a U-Boot-style environment editor (`env_htab`, `himport_r`,
  `crc32`, `mtd_open`, `erase_mtd`) whose **default environment template** is

  ```
  image_boot=0
  image0_stats=0
  image1_stats=0
  ipaddr=192.168.1.1
  netmask=255.255.255.0
  serverip=192.168.1.99
  ```

  and
* `bin/tendaupgrade` — which sets it.

Searched for all three keys in: the decompressed stage-1 loader (229,584 bytes),
the stock 4.4.176 kernel image, every script under `/etc_ro` and `/etc`, and the
flash dumps themselves. **No hits.** The live environment in mtd1 (`0x2f004`)
contains only `disaster_web=1`, `ipaddr`, `netmask`, `serverip` — `image_boot` is
not even present.

So the flag is vestigial on this firmware: a bootloader version that consumed it
is not the one in this flash image, and nothing else looks at it. Two practical
consequences:

* a flashing procedure for this port does **not** need to reproduce
  `env set image_boot 1`, and cannot brick the unit by omitting or clearing it;
* the vendor's "unlock" is likewise not a security gate on this build.

The env editor itself is still relevant for recovery — `bin/env` is the tool that
writes the same 4-key environment the loader's disaster-web path reads.
