# Stock evidence — RTL8832BR driver, firmware and RF params on RX2L Pro

All paths are inside our own dumps; nothing here was poked live.
Dumps: `/tmp/rx_dump/rootfs/x` (extracted stock squashfs, 4.4.176),
`/tmp/work3/kernel.lzma.out` (decompressed stock vmlinux, MIPS **LE**,
KALLSYMS=y, load base `0x80000000`, so *file offset = vaddr − 0x80000000*),
`/tmp/rx_dump/logs/syslog/sys_info.log` (stock `/proc/*` snapshots).

## 1. The driver is built into the kernel, not a module

* `/tmp/rx_dump/rootfs/x/etc_ro/init.d/rcS:57` — `#无线驱动已编入内核，此处不加载`
  ("the wireless driver is compiled into the kernel, not loaded here"), and
  lines 60–68 are **commented-out** `insmod`s, including:

  ```sh
  #insmod /lib/modules/rtk_wifi6.ko \
  #               rtw_ht_enable=2 rtw_vht_enable=2 rtw_he_enable=2 \
  #               rtw_drv_log_level=2 phl_log_level=2
  ```
* `/tmp/rx_dump/rootfs/x/lib/modules/` contains 24 vendor `.ko`s
  (`wifibase.ko`, `fast_l2.ko`, `bm.ko`, …) and **no wifi driver at all**;
  filtering `4.4.176/modules.builtin` for wifi/pci-ish names returns only
  `rtkxxpart.ko` and `8250_pci.ko`.
* `build -> /home/chenpeng/work/rx2l/develop_svn4992/bsp/kernel/rtl8197h_v3.6.0_4.4.176`
  — the vendor BSP name (Tenda internal project `rx2l`, `develop_svn4992`).
* The whole Realtek **G6 / Wi-Fi 6 SKD ("PHL") architecture** is present in the
  kernel image: module-parameter strings `rtk_wifi6.rtw_fw_file_path`,
  `rtk_wifi6.rtw_fw_wow_file_path`, `rtk_wifi6.rtw_fw_mp_bt_file_path`,
  `rtk_wifi6.rtw_iqk_fw_offload`, `rtk_wifi6.phl_log_level`,
  `rtk_wifi6.rtw_band_type`, `rtk_wifi6.rtw_FileMaskEfuse`, … plus log strings
  `PHL: …`, `%s%sRTW: …`, `rtw_phl_*`, `halrf_*_8832br`.
  Function symbols (kallsyms) prove the 8832BR HAL is compiled in:
  `_halrf_set_power_8832br`, `_halrf_get_tssi_trim_8832br`,
  `halrf_config_8832br_write_radio_a_reg_to_fw`, `_halrf_do_non_dbcc_txgapk_8832br`,
  and the chip-name strings `8832br`, `rtl8832br`, `rtl8832bre`.
  Build flags visible in rodata: `CONFIG_WLAN0_RFE_TYPE_50`,
  `CONFIG_WLAN1_RFE_TYPE_50`, `AP_5G`.

## 2. PCI IDs the stock driver binds

`struct pci_device_id` array (28-byte entries on MIPS32: vendor, device,
subvendor, subdevice, class, class_mask, driver_data) at file offset
**`0xb77860`**:

| file offset | vendor | device | driver_data | SDK name (from the vendor table in patch 100 of `/tmp/rebase-base` `830c08cf`) |
|---|---|---|---|---|
| `0xb77860` | `0x10ec` | **`0xb814`** | 0 | `RTL8852A` |
| `0xb7787c` | `0x10ec` | **`0xb832`** | 5 | **`RTL8832BR` ← our chip** |
| `0xb77898` | `0x10ec` | **`0x0192`** | 4 | `RTL8192XB` (in-SoC 2.4 GHz presented on the internal PCIe) |
| `0xb778b4` | terminator (all zero) | | | |

Raw bytes (`0xb7787c`): `ec 10 00 00 32 b8 00 00 ff ff ff ff ff ff ff ff
00 00 00 00 00 00 00 00 05 00 00 00`. Subvendor/subdevice/class are
`ANY`/unset, so **no subsystem filter** — any `10ec:b832` card binds.

Sibling-board check: `/tmp/rebase-base`
`target/linux/realtek/dts/rtl8197f_tenda_ac23.dts:262` declares
`compatible = "pci10ec,b814"` → that board's 5 GHz client is an
**RTL8852A**, a different G6 chip, *not* our family member. The node is
toby's own non-mainline convention (`realtek,radio-index`, `nvmem-cells`,
`realtek,factory-format = "tenda-kv"` — used by their userspace MAC/factory
scripts, not by any kernel driver). What we can copy from it is the
**PCIe plumbing block**, which is identical to ours:
`&pcie0 { interrupt-parent = <&intc>; interrupts = <21 4>; ranges =
<0x01000000 0 0x18c00000 0x00c00000 0 0x00200000>,
<0x02000000 0 0x19000000 0x01000000 0 0x01000000>; status = "okay"; }`
— ICTL source 21 routed to CPU line 5, and **bus address == CPU address**.

## 3. Interrupt, from stock runtime

`/tmp/rx_dump/logs/syslog/sys_info.log` (`cat /proc/interrupts` snapshot):

```
  2:        243      MIPS   2  IRQ cascade
  4:          0      MIPS   4  eth0
  5:     968696      MIPS   5  rtk_wifi6      <-- RTL8832BR, INTx, CPU line 5
  6:    4711710      MIPS   6  wlan1          <-- 2.4 GHz rtl8192cd
  7:     661731      MIPS   7  timer
 17:        243      ICTL  serial
ERR:         13
```

Naming inversion to remember: **`wlan0` = 5 GHz (rtk_wifi6), `wlan1` = 2.4 GHz
(rtl8192cd)** — confirmed by the same file's `/proc/km/km_online_ip` table,
which tags `wlan0-vap1` as `5G` and `wlan1-vap1` as `2.4G`, and independently by
`/tmp/rebase/mr1500x-openwrt/files/etc/init.d/mr1500x-wifi:18-20`.

## 4. Firmware: embedded, and there is no `.bin` to ship

* Stock rootfs has **no `/lib/firmware`** (and no `*.fw` / wifi `*.bin`
  anywhere under `/tmp/rx_dump/rootfs/x`; the only `.bin` strings in the kernel
  are `rtl8192xbfw_rom.bin`, `hal8192xb_msg_symbol.bin`, `symbol.bin` and
  `/system/etc/firmware/rtlwifi/FW_WoWLAN.BIN` — all 8192xb/WoW leftovers).
* The 8832BR image is downloaded by driver code, not by `request_firmware`:
  `rtw_hal_download_fw`, `__fwhdr_download`, `Load FW RAM code OK`,
  `InitDownload FW Fail`, `dbg_fw_mem_dl_error_cnt`. The equivalent G6 build
  switches the file path off (`CONFIG_FILE_FWIMG=n`) and toby's note says the
  FW "comes from the embedded G6 firmware array"
  (`/tmp/rebase-base` `reverse-engineering/wr1500-wifi6/LINUX66-DRIVER.md`).
* `rtw_fw_file_path` / `rtw_fw_wow_file_path` / `rtw_fw_mp_bt_file_path` exist
  as override params but are unused on stock.
* Consequence: **the only copy of the 8832BR firmware we can get is the byte
  array inside a vendor GPL drop** (TP-Link/Mercusys), which is precisely what
  makes route (b) hard and route (a) easy. See `rtw89-gap.md` §4 for the
  licensing judgement.

## 5. RF / PHY parameters: file-loaded, board-specific, mandatory

Stock layout `/tmp/rx_dump/rootfs/x/etc_ro/wlan/rtl8832bre/RFE50/` (40 files,
dated 2026-04-13 in the dump):

* active set in the `RFE50/` root: `PHY_REG.txt` 25327 B (header
  `//rtl8192xb version = 31`), `PHY_REG_GAIN.txt` 3550 B, `RadioA.txt` 20235 B,
  `RadioB.txt` 20063 B, `TXPWR_ByRate.txt` 2327 B, `TXPWR_LMT.txt` 24931 B,
  `TXPWR_LMT_RU.txt` 16193 B, `TXPWR_TrackTSSI.txt` 2403 B;
* `AP_5G/`, `AP_2G/`, `PON_5G/`, `PON_2G/` per-mode overrides. **`AP_5G/PHY_REG.txt`
  is a 33-byte stub (`//AP_5G_PWRTAB_00` + `0xffff 0xffff`), as are
  `AP_5G/RadioA.txt`/`RadioB.txt` (37 B)** — i.e. for the AP_5G role the big
  tables come from the parent directory, only the TXPWR_* files are real
  (`AP_5G/TXPWR_LMT.txt` 24953 B, `AP_5G/TXPWR_LMT_RU.txt` 16215 B,
  `AP_5G/TXPWR_ByRate.txt` 2379 B, `AP_5G/TXPWR_TrackTSSI.txt` 2396 B).
* Load mechanism: `/etc_ro/init.d/rcS:69`
  `iwpriv wlan0 flash_set para_path=/etc/wlan/` (`/etc` is a symlink to
  `/var/etc`, populated from `/etc_ro` during boot). Driver strings:
  `'flash_set`, `'flash_set_name`, `'flash_set_oft`, `para_path`,
  `PHY_REG.txt`, `PHY_REG_GAIN.txt`, `RadioA.txt`, `RadioB.txt`,
  `TXPWR_ByRate.txt`, `TXPWR_LMT.txt`, `TXPWR_LMT_RU.txt`,
  `TXPWR_TrackTSSI.txt`, `TXPWR_LMT_6G.txt`, `rtw_decrypt_phy_file`, and
  `rtw_efuse_mask_file_read` / `rtw_is_file_readable` failure logs.
  This is an **eFEM** board (`rfe_type >= 50`): per
  `/tmp/rebase/mr1500x-openwrt/files/etc/init.d/mr1500x-wifi:30-34`, without
  these files the synthesizer never locks and 5 GHz is dead.
* 2.4 GHz calibration is a completely separate path (CFG partition →
  `/etc/wlan_rfhwdata` → `/bin/rfhw_convert` → `/tmp/rfhw.sh`, rcS:73-86);
  do **not** map AP_2G/PON_2G onto the 2.4 GHz radio — toby says the same.

**Cross-check against the WR1500 R76 package** (`/tmp/rebase-base`
`package/kernel/rtl8832br-wr1500/Makefile`, SHA256 gate over 16 files, read
with `git show HEAD:...` because that checkout is sparse). Comparing our stock
files to that list:

| | verdict |
|---|---|
| **12 match byte-for-byte** — `PHY_REG.txt`, `PHY_REG_GAIN.txt`, `TXPWR_LMT.txt`, `TXPWR_LMT_RU.txt`, `TXPWR_TrackTSSI.txt`, `AP_5G/PHY_REG.txt`, `AP_5G/PHY_REG_GAIN.txt`, `AP_5G/RadioA.txt`, `AP_5G/RadioB.txt`, `AP_5G/TXPWR_LMT.txt`, `AP_5G/TXPWR_LMT_RU.txt`, `AP_5G/TXPWR_TrackTSSI.txt` | same PHY/IP generation, tables are interchangeable |
| **4 differ** — `RadioA.txt`, `RadioB.txt` (radio front-end gain tables), `TXPWR_ByRate.txt`, `AP_5G/TXPWR_ByRate.txt` | **per-board RF data: we must ship ours**, extracted from our dump at build time |

Root-level prefixes of ours: `RadioA.txt 5426e285e60c3af8…`,
`RadioB.txt f456f35b92285ef1…`, `TXPWR_ByRate.txt 48a7d1061f5d7471…`,
`AP_5G/TXPWR_ByRate.txt b1516c01bd5521e4…`. Full hashes are recorded in
`skd-attempt/files/rfe50-manifest.sha256` so the build can verify it extracted
the right ones. **Do not commit these files** — vendor RF data, same rule the
mr1500x project states for its 39 tables.

## 6. Userspace control surface the SKD exposes (measured on siblings)

* proc: `/proc/net/rtk_wifi6/<ifname>/country_code` (write ISO code), plus a
  large `phl`/`iwpriv` debug surface (`phl trx test pattern`, `phlRx MAP`,
  `phlTx MAP`, `mib_all`).
* `iwpriv <if> set_mib func_off=0` is required on the **rtl8192cd** root iface
  before VAPs, else every data frame is dropped silently
  (`mr1500x-wifi:326`).
* `iwpriv wlan0 flash_set para_path=…` — the RF table loader (§5).
* Hard-earned warnings from `/tmp/rebase/mr1500x-openwrt/README.md` +
  `files/etc/init.d/mr1500x-wifi`, all reproduced in `skd-attempt/README.md`:
  OpenWrt's stock `/lib/netifd/wireless/mac80211.sh` **hangs this driver**
  (`iw phy add interface`), a hung radio survives reboot and clears only on a
  power cycle, never `ifup` the root iface before tuning, `txpower` changes can
  hang it, and their HT is capped at HT40 with no VHT/HE wired.

## 7. Open questions

1. **BAR0/BAR2 sizes and I/O vs MMIO split on our strap.** toby measured
   BAR2 ≥ 1 MiB at `0x19000000` on *their* board; ours is presumed identical
   (`routes.md`). Verify on RAM-boot with
   `skd-attempt/files/8832-bringup-diag` (config-space dump, no MMIO probe).
2. Whether the vendor `rtw_fw_file_path` override can point at an extracted
   copy of the embedded array — useful only for debugging FW versions.
3. `driver_data` 0/4/5 semantics beyond the SDK labels (which HAL set each
   selects) — answered by the patch-100 table, but only for the TP-Link G6
   revision; our BSP is Realtek UGW `rtl8197h_v3.6.0`.
