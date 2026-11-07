# Calibration — 2.4 GHz RF, Tenda RX2L Pro (RTL8197F integrated WMAC)

Everything below is measured from our own dumps
(`/tmp/rx_dump/flash/mtd2_CFG.bin`, `/tmp/rx_dump/rootfs/x`) and from the two
published 6.18 HAL builds. No per-unit value, MAC, serial or PIN is written
here; where a value is quoted it is the *shipped default* file or a masked
relation.

## 0. TL;DR

* Stock calibrates from a **NUL-separated key/value text store** in mtd2
  `CFG`, not from a packed binary blob and not from `/lib/firmware`.
* It converts those keys into `iwpriv … set_mib` lines with
  `/bin/rfhw_convert` and runs them (`/tmp/rfhw.sh`) **once, at boot, before
  any interface is brought up**.
* The destination names are genuine rtl8192cd MIB keys. So the 6.18 driver
  needs the *same words on the same ioctl*, nothing more.
* Our unit's 2.4 GHz group contains: `xcap`, `thermal1`, `thermal2`, and the
  `pwrlevelCCK_A/B`, `pwrlevelHT40_1S_A/B`, `pwrdiffHT20`, `pwrdiffOFDM`
  tables. That is a complete 2.4 GHz calibration set.
* **If calibration is missing the radio still comes up and still
  associates** — but it runs on the driver's compiled-in tables: the crystal
  trim is skipped (`xcap == 0xff` ⇒ the register write is not performed, or
  `0 < xcap < 0x3f` is false ⇒ the fine-tuning loop is skipped), TX power
  comes from `data_PHY_REG_PG_8197Fmp_TypeN.c`, and the cfg80211 build
  additionally falls back to a **hard-coded fake MAC**. So the failure is
  invisible from the console and visible on a spectrum analyser. That is the
  first-boot risk in one sentence.

---

## 1. What stock actually does — quoted

`/tmp/rx_dump/rootfs/x/etc_ro/init.d/rcS`, the "射频参数配置" (RF parameter
configuration) block, lines 72-85:

```sh
#射频参数配置
NMTD=$(grep CFG /proc/mtd | awk -F : '{print $1}')
dd if=/dev/"$NMTD" of=/etc/wlan_rfhwdata count=8192 ibs=8 skip=1
#如果apmib分区数据为空，则拷贝默认配置,暂时先用HW_XCAP_WLAN0_AX做判断
blank=$(grep HW_XCAP_WLAN0_AX /etc/wlan_rfhwdata && echo 0 || echo 1)
if [ "$blank" -eq 1 ]; then
        echo "apmib is blank, copy default apmib now"
        dd if=/etc/wlan/.wlan_rfhw.cfg of=/etc/wlan_rfhwdata
fi
#射频参数名字转换
/bin/rfhw_convert
chmod +x /tmp/rfhw.sh
#向驱动配置射频参数
/tmp/rfhw.sh
```

Two corrections to the shared brief (FACTS.md §6), because they change what we
have to ship:

1. **It is not 8192 bytes.** `count=8192 ibs=8 skip=1` is 8192 *blocks of 8
   bytes* starting after block 0 ⇒ a **65 536-byte read from flash offset 8**,
   i.e. the whole 64 KiB CFG partition after its 8-byte header. mtd2 is exactly
   65 536 bytes (`/proc/mtd`: `mtd2 00010000 00010000 "CFG"`, offset 196608), so
   stock slurps the entire partition. Our converter must read the same span; a
   `count=8192` *byte* read would stop exactly where the interesting keys are.
2. **The header is 8 bytes, and it is not text.** `mtd2_CFG.bin` starts
   `cd 74 b1 d5 00 00 00 00` — a 4-byte tag plus a BE u32 that is 0 here.
   Everything from offset 8 on is `KEY=VALUE\0` records; the partition is
   roughly 92 % free (60 358 NUL bytes in that span) and holds 164 distinct
   keys.

The blank-check is `grep HW_XCAP_WLAN0_AX`, and our unit *has* that key, so on
our device the fallback `/etc_ro/wlan/.wlan_rfhw.cfg` is **never used** — we
are reading the factory store. (That default file is still worth knowing:
`/tmp/rx_dump/rootfs/x/etc_ro/wlan/.wlan_rfhw.cfg`, 1573 bytes, 43 keys, and it
is the only place that states `HW_RFE_TYPE_WLAN0=0` — see §5.)

## 2. `/bin/rfhw_convert`, decoded

A 9 716-byte MIPS **little-endian** uClibc executable (consistent with FACTS
§2), built with `Realtek MSDK-6.4.1`. Its whole job is one static array:
`g_convert_table`, 948 bytes at vaddr `0x411bb0` = **79 entries of
{char \*apmib_key; char \*driver_name; u32 target}**. Read from
`/etc/wlan_rfhwdata`, written to `/tmp/rfhw.sh`, one line per matched key:

```
#!/bin/sh
/bin/iwpriv <iface> <set_mib|flash_set> <driver_name>=<value>
```
(from the format string `"%s %s %s %s=%s"` plus the literals `/bin/iwpriv`,
`wlan0`, `wlan1`, `set_mib`, `flash_set`).

The `target` field is `4*(is_AX) + 1 + wlan_index`, which the table proves by
construction:

| target | iface + verb | meaning |
|---|---|---|
| 1 | `wlan0 set_mib` | legacy RTL8192-family HAL, radio group `HW_*_WLAN0_*` |
| 2 | `wlan1 set_mib` | legacy RTL8192-family HAL, radio group `HW_*_WLAN1_*` |
| 5 | `wlan0 flash_set` | Wi-Fi 6 PHL stack (`rtk_wifi6`), `HW_*_WLAN0_AX` |
| 6 | `wlan1 flash_set` | Wi-Fi 6 PHL stack, `HW_*_WLAN1_AX` |

The `set_mib` rows that matter for us, verbatim from the decoded table
(apmib key → driver name):

```
HW_WLAN1_11N_XCAP            -> xcap             HW_WLAN1_11N_THER      -> thermal1
HW_WLAN1_11N_TSSI1           -> tssi1            HW_WLAN1_11N_THER_2    -> thermal2
HW_WLAN1_11N_TSSI2           -> tssi2            HW_WLAN1_11N_THER_3    -> thermal
HW_WLAN1_11N_TSSI_ENABLE     -> tssi_enable      HW_WLAN1_MIMO_TR_MODE  -> MIMO_TR_mode
HW_WLAN1_TX_POWER_CCK_A      -> pwrlevelCCK_A    HW_WLAN1_TX_POWER_CCK_B      -> pwrlevelCCK_B
HW_WLAN1_TX_POWER_HT40_1S_A  -> pwrlevelHT40_1S_A
HW_WLAN1_TX_POWER_HT40_1S_B  -> pwrlevelHT40_1S_B
HW_WLAN1_TX_POWER_DIFF_HT40_2S -> pwrdiffHT40_2S
HW_WLAN1_TX_POWER_DIFF_HT20  -> pwrdiffHT20
HW_WLAN1_TX_POWER_DIFF_OFDM  -> pwrdiffOFDM
HW_WLAN1_TX_POWER_TSSI_CCK_A/B, HW_WLAN1_TX_POWER_TSSI_HT40_1S_A/B/C/D
                             -> pwrlevel_TSSI*  (TSSI-trimmed variants)
```

**These are not invented names.** Every one is an entry in the driver's own MIB
lookup table, in all three candidate trees: `xcap` at
`8192cd_ioctl.c:1456` (`INT_T, _OFFSET(dot11RFEntry.xcap)`), `tssi1` at
`:1458`, `pwrlevelCCK_A` at `:1417` (`BYTE_ARRAY_T,
_OFFSET(dot11RFEntry.pwrlevelCCK_A)`), `pwrdiffHT20` at `:1426`,
`MIMO_TR_mode` at `:1472-1478`. Which is the whole reason this calibration path
transfers to 6.18 unchanged: it is an *interface of the driver*, not of the
kernel.

### Which radio group is the 2.4 GHz WMAC? (this is the trap)

The vendor contradicts itself on interface *names*, three ways, and the group
*index* has to be settled from the data:

* `/tmp/rx_dump/rootfs/x/etc_ro/eth_name_conf.txt` — `wl2_4:wlan0;
  wl5_0:wlan1;` ⇒ 2.4 GHz = wlan0.
* `/tmp/rx_dump/RouterCfm.cfg` (the live config) — `wlan0_band=5`,
  `wlan0_workmode=ap`, `wlan1_workmode=apclient` ⇒ 2.4 GHz = wlan1.
* `rcS` — `/bin/iwpriv wlan0 flash_set para_path=/etc/wlan/`, and FACTS §6
  attributes `flash_set` to the RTL8832BR ⇒ **wlan0 is the AX chip**, so
  again 2.4 GHz = wlan1.

The apmib keys settle it independently of all three, because the *shape* of the
payload can only belong to one band: `HW_WLAN1_TX_POWER_CCK_A=
5c5c5c5a5a5a5a5a5a5b5b5b5b5b` is **14 bytes**, and 14 channels exist only at
2.4 GHz; `HW_WLAN1_TX_POWER_HT40_1S_A/B` likewise; whereas
`HW_5G_BW40_1S_TSSI_A/B` and `HW_RX_GAINGAP_5GL/5GM/5GH_AB` are 5 GHz shapes
and `HW_XCAP_WLAN0_AX` is, by its own `_AX` suffix, the Wi-Fi 6 chip. And the
MACs agree: on the dumped unit `HW_WLAN1_WLAN_ADDR == TENDA.lan_mac + 4`
exactly, which is the relationship our own 4.14 script measured for the
**2.4 GHz** AP MAC, while `HW_WLAN0_WLAN_ADDR == lan_mac + 0x14` matches what
that same script measured for 5 GHz.

⇒ **Radio group `WLAN1` (`target` 2, `iwpriv wlan1 set_mib …`) is the
RTL8197F integrated 2.4 GHz WMAC.** `rfhw-convert` defaults to `--group 1` for
that reason, with `--group 0` present so a one-flag change covers the other
reading. Do not encode this in the driver — it is a userspace naming fact.

## 3. What the 6.18 driver actually wants

The driver we adopt has a *different* calibration path, and understanding the
mismatch is the design decision.

iptime's `realtek_load_8197f_calibration()`
(`package/kernel/rtl8192cd/src/8192cd_cfg80211.c:1085-1163`, called from
`8192cd_osdep.c:16611` for the root interface) wants a **packed 1600-byte
blob**:

```c
np   = of_find_compatible_node(NULL, NULL, "realtek,rtl8197f-wifi");
cell = of_nvmem_cell_get(np, "calibration");
hw   = nvmem_cell_read(cell, &len);
if (len != 0x640 || !is_valid_ether_addr(hw->macAddr) ||
    !hw->xCap || hw->xCap >= 0x3f ||
    !hw->Ther || hw->Ther == 0xff)  { ret = -EINVAL; goto out; }
for (i = 0; i < MAX_2G_CHANNEL_NUM; i++)
        if (!hw->pwrlevelCCK_A[i] || hw->pwrlevelCCK_A[i] > 0x3f || …) { -EINVAL }
```
and then `memcpy`s the seven 14-byte arrays into
`priv->pmib->dot11RFEntry.{pwrlevelCCK_A,pwrlevelCCK_B,pwrlevelHT40_1S_A,
pwrlevelHT40_1S_B,pwrdiffHT40_2S,pwrdiffHT20,pwrdiffOFDM}`, sets `.ther`,
`.xcap`, and copies `hw->macAddr` over `dot11OperationEntry.hwaddr`,
`dot11Bss.bssid` and `rtk->root_mac`.

`struct hw_wlan_setting` is `__PACK`ed (`8192cd_cfg80211.c:119-242`) and its
2.4 GHz-relevant prefix is byte-for-byte the layout base B documents in
`target/linux/realtek/rtl8197f/oem-calibration-schema.json` — same offsets,
same sizes:

| offset | size | field | schema key (ac23) | our apmib key |
|---|---|---|---|---|
| 0 | 6 | `macAddr` | — | `HW_WLAN1_WLAN_ADDR` |
| 48 | 14 | `pwrlevelCCK_A` | `TX_POWER_CCK_A` | `HW_WLAN1_TX_POWER_CCK_A` |
| 62 | 14 | `pwrlevelCCK_B` | `TX_POWER_CCK_B` | `HW_WLAN1_TX_POWER_CCK_B` |
| 76 | 14 | `pwrlevelHT40_1S_A` | `TX_POWER_HT40_1S_A` | `HW_WLAN1_TX_POWER_HT40_1S_A` |
| 90 | 14 | `pwrlevelHT40_1S_B` | `TX_POWER_HT40_1S_B` | `HW_WLAN1_TX_POWER_HT40_1S_B` |
| 104 | 14 | `pwrdiffHT40_2S` | `TX_POWER_DIFF_HT40_2S` | *(absent on our unit)* |
| 118 | 14 | `pwrdiffHT20` | `TX_POWER_DIFF_HT20` | `HW_WLAN1_TX_POWER_DIFF_HT20` |
| 132 | 14 | `pwrdiffOFDM` | `TX_POWER_DIFF_OFDM` | `HW_WLAN1_TX_POWER_DIFF_OFDM` |
| 146 | 1 | `regDomain` | — | |
| 147 | 1 | `rfType` | — | `HW_RFE_TYPE_WLAN0` (default file only) |
| 148 | 1 | `ledType` | — | |
| 149 | 1 | `xCap` | `11N_XCAP` | `HW_WLAN1_11N_XCAP` |
| 150 | 1 | `TSSI1` | `11N_TSSI1` | `HW_WLAN1_11N_TSSI1` |
| 151 | 1 | `TSSI2` | `11N_TSSI2` | `HW_WLAN1_11N_TSSI2` |
| 152 | 1 | `Ther` | `11N_THER` | `HW_WLAN1_11N_THER` |

base B's schema says the mapping exists on two *other* boards' layouts
(`rd05` `wlan_root_offset 1033`, `radio_stride 2507`; `ac23` `wlan_root_offset
13`, `radio_stride 3395`) — and its own header line is the sentence that
applies to us:

> "RD05 offsets feed the Linux6 H601 parser. **AC23 uses the Tenda key/value
> store, not these packed offsets.**"

RX2L Pro is the AC23 case. So:

* **The nvmem/packed route is unavailable on our unit.** There is no packed
  blob to point a cell at: `H601`, the signature both base A and base C read
  out of the boot partition to find `HW_SETTING_OFFSET` (`rtl8192cd.sh`'s
  `rtl_factory_mac()` even asserts it: `[ "$(dd … skip=0x20000 count=4)" =
  "H601" ] || return 1`), appears **nowhere** in mtd1..mtd7 of our dump —
  searched as raw bytes across `mtd1_Bootloader.bin`, `mtd2_CFG.bin`,
  `mtd3_KernelFS.bin`, `mtd4_RootFS.bin`, `mtd5_CFM.bin`,
  `mtd6_CFM_BACKUP.bin`, `mtd7_MISC2.bin`. Zero hits.

* **`set_mib` is available and is what the vendor used.** The adopted driver
  keeps `.ndo_do_ioctl = rtl8192cd_ioctl` (`8192cd_osdep.c:13694`, `:13720`,
  `:13743`, `:18299`, `:18378`) and `dev->wireless_handlers`
  (`:9782`, `:16577`) in its cfg80211 build, so `SIOCDEVPRIVATE+1`
  (`RTL8192CD_IOCTL_SET_MIB`, `8192cd_ioctl.c:75`) reaches the same
  `rtl8192cd_mib_addr[]` table even though a wiphy is registered. That is why
  `rtlmib` is in the staged package set.

⇒ **Adopted design: userspace converter, no kernel patch.**
`files/…/usr/sbin/rfhw-convert` + `files/…/etc/hotplug.d/net/
99-rfhw-rx2lpro` replace `dd + rfhw_convert + /tmp/rfhw.sh` one-for-one. The
in-driver nvmem loader is left compiled but inert: with no `nvmem-cells` on the
`realtek,rtl8197f-wifi` node, `of_nvmem_cell_get()` returns `-ENOENT`,
`realtek_load_8197f_calibration()` returns that error, and the **return value
is ignored by its only caller** (`8192cd_osdep.c:16611` is a bare statement),
so nothing aborts. Verified, not assumed — it is the reason this route cannot
break the boot.

The alternative (synthesise a 1600-byte blob at sysupgrade time and point a
cell at it) was rejected: it needs either a writable calibration partition we
do not have, or a generated blob baked into the image, which would put a
**per-unit MAC in a published image** — forbidden by FACTS §0.

## 4. What happens if calibration is missing — the first-boot risk

Trace of the "no calibration" path, in the driver we are shipping:

1. **The interface still appears, still opens, still beacons.** Nothing on
   this path is gated by the calibration values. `wifi detect` sees the wiphy,
   hostapd reaches `AP-ENABLED`, clients associate. This is why the risk is
   easy to under-weight: a missing-calibration board *looks* fine.
2. **Crystal cap (`xcap`) is silently not applied.** The register write is
   guarded: `if (priv->pmib->dot11RFEntry.xcap != 0xff)` then
   `phy_set_bb_reg(priv, 0x24, 0xF0, xcap & 0x0F)` /
   `phy_set_bb_reg(priv, 0x28, 0xF0000000, (xcap & 0xF0) << 28)` and the
   ramping loop only runs `if (xcap > 0 && xcap < 0x3F)` — three separate
   bail-out conditions, none of which logs
   (`8192cd_hw.c:17885-17894`; `xcap` is also fed to PHYDM as
   `ODM_CMNINFO_X_CAP_SETTING` at `8192cd_hw.c:15350`, i.e. it is consumed at
   init, not continuously). Wrong/absent xcap means the synthesized clock runs
   off the untrimmed 12 MHz reference: **carrier-frequency offset**, degraded
   EVM, more retransmits, and a real risk of failing the emission mask of
   whatever country the unit is in. Symptom: usable at 1 m, marginal at 5 m,
   clients "connected but slow", and no kernel message about it.
3. **TX power comes from the compiled-in tables, not from the unit.**
   `pwrlevelCCK_A` etc. default to what `set_mib_default()`/`data_PHY_REG_PG_
   8197Fmp_Type*.c` provide, and `8192cd_ioctl.c:6443` documents the
   convention: `if (pwrlevelCCK_A == 0) { /* use default value */ }`. So
   the unit transmits at the reference board's power. `TD_WLAN0_POWER=23` and
   `FLAG_OF_CE_LIMIT_POWER=open` in our apmib say the factory expected a
   ~23 dBm-class budget with CE limiting off; if the baked default is lower
   the radio is merely weak, if it is higher it is non-compliant. **Either way
   this is a "power/data bug", not a "no interface" — and the interface coming
   up is exactly why it must be verified with a spectrum analyser or a
   RSSI-vs-distance sweep, not with `iw` output.**
4. **The MAC becomes a constant.** This is the concrete, checkable one. When
   the loader does not run, `rtk->root_mac` comes from
   `char rtk_fake_addr[6] = {0x00,0xe0,0x4c,0xcc,0xdd,01}; //mark_dual, FIXME
   if wlan_mac readable` (`8192cd_cfg80211.c:103`, applied at `:8049-8050`,
   `rtk_fake_addr[3] += idx`). base A hit the same class of bug and wrote it
   down: *"Без своего MAC у обоих радио один BSSID 00:e0:4c:81:86:86, и Android
   показывает только одну из двух сетей (проверено на RX23302 02.10.2026)"* —
   one BSSID for every unit, clients collapsing two SSIDs into one entry.
5. **Thermal tracking is off, but only because efuse is compiled out anyway.**
   `ther_trim_efuse()` (`8192cd_hw.c:33031`) reads the on-die trim at efuse
   offset `0x50` and clamps |value| ≤ 12, but it and `power_trim()` (k-free)
   are both inside `#ifdef CONFIG_RF_KFREE_SUPPORT`
   (`8192cd_cfg.h:4924-4932`), which the package does not define; likewise
   `EN_EFUSE` needs `CONFIG_SOC_ENABLE_EFUSE` (`8192cd_cfg.h:780`) and is off.
   So the driver today ignores the on-die efuse entirely — see §5, that is an
   opportunity, not a problem.
6. **`/etc/Wireless/RTL8192CD.dat` is still live and still self-writing.**
   `#define CFG_FILE_PATH "/etc/Wireless/RTL8192CD.dat"` survives at
   `8192cd_comapi.c:3614` in the 6.18 tree. Our existing 4.14 seeder already
   paid for that measurement ("the driver rewrites it on every interface-up,
   8192cd_comapi.c:3431, applied from 8192cd_osdep.c:7703") and the mitigation
   — `ln -s /tmp /etc/Wireless` — carries over unchanged. Keep it: on a
   squashfs-over-OVR rootfs a driver that writes its own config on every
   `wifi up` is a flash-write-per-reload bug.

**Checklist consequence.** "2.4 GHz is up" is *not* proof of calibration. The
first-boot check has to be: `iwinfo wlan1 hwaddr` (or `ip l`) is **not**
`00:e0:4c:*`, and `rtlmib wlan1 get xcap` / `cat /proc/wlan1/mib_rf` shows the
factory `xcap`, not `0xff`/0. Both are cheap and both fail loudly.

## 5. The one RF knob the dumps do not settle: RFE type

`dot11RFEntry.rfe_type` selects which front-end the PHY sequences for, and it
is **compile-time only** in all three trees —
`8192cd_osdep.c:13323-13341` maps `CONFIG_SOC_RFE_TYPE_0..6` straight onto
`rfe_type = 0..6` and then `panic_printk("RFE TYPE =%d\n", …)`. There is no
module parameter and no MIB write on the cfg80211 path (`rfType` exists as a
struct field but the reachable `set_mib` name in these builds is `rfe`; our
unit's CFG does not contain `HW_RFE_TYPE_WLAN0` or `_WLAN1` at all).

Evidence, kept separate because it conflicts:

| source | says |
|---|---|
| our unit's factory apmib | **no** `HW_RFE_TYPE_*` key ⇒ stock never pushed one ⇒ stock used *its own* compiled-in default, which we have not extracted |
| stock fallback `/etc_ro/wlan/.wlan_rfhw.cfg` | `HW_RFE_TYPE_WLAN0=0` — and this is Tenda's own default for this product line |
| base A, RX23302 (RTL8197**G**) | `rx23302.mk` `-DCONFIG_SOC_RFE_TYPE_0` |
| base C, A2004MU (RTL8197F) | `CONFIG_SOC_RFE_TYPE_1=y CONFIG_SOC_EXT_PA=y CONFIG_SOC_EXT_LNA=y` — an externally amplified design |
| base B profiles | `mw5` integrated `RFE5`; `wrd12gn` integrated `RFE0`; `rd05` `0`; `ac23` `unknown` |

An AX-class budget consumer router with 2×2 internal antennas is much more
likely RFE0 than RFE1+EXT_PA, and Tenda's own default file says 0, so the
staged Makefile defaults to **`RX2LPRO_RFE_TYPE?=0`** and makes it one
`make` variable to flip (`RX2LPRO_RFE_TYPE=1 …` pulls in `SOC_EXT_PA`/
`SOC_EXT_LNA` as base C has them). Two things to do about it, in order:
(a) the boot log prints `RFE TYPE =N` unconditionally, so the *first CI-built
image on the console* tells us the number stock's successor is using — compare
TX RSSI/`iwinfo … quality` against a RFE1 rebuild; (b) if a vendor blob can be
dusted off the shelf, the compiled-in PG table name in the stock kernel
(`data_PHY_REG_PG_8197Fmp_TypeN`) pins it exactly. **Do not treat 0 as settled
until (a) has been run.**

## 6. File / permission layout to ship in `files/`

```
files/target/linux/realtek/base-files/usr/sbin/rfhw-convert          0755
files/target/linux/realtek/base-files/etc/hotplug.d/net/99-rfhw-rx2lpro  0755
files/target/linux/realtek/base-files/etc/uci-defaults/12_wireless-2g-rx2lpro 0755
package/utils/rtlmib/  -> /usr/sbin/rtlmib                           0755
```

Runtime layout the image must present, and who creates it:

| path | by | mode | note |
|---|---|---|---|
| `/dev/mtd2`, `/dev/mtd2ro` | kernel `CONFIG_MTD_CHAR*` | 0600 / 0444 | the converter reads **only**; nothing here may ever write mtd2 (FACTS §0) |
| `/proc/mtd` | kernel | 0400 | `grep -m1 CFG` is the only lookup — label, never index |
| `/proc/wlan*/mib_all`, `/proc/wlan*/mib_rf` | driver, `CONFIG_RTL_PROC_NEW` | — | identity test for "this netdev is rtl8192cd", and the place to read xcap back for verification |
| `/etc/Wireless` | uci-defaults, first boot | symlink → `/tmp` | `RTL8192CD.dat` lands in RAM; `[ -e /etc/Wireless ] || ln -s /tmp /etc/Wireless` |
| `/tmp/sysinfo/board_name` | `board.d/00_hwdetect` | | must read `rx2l-pro` or the seeder exits 0 doing nothing |
| ~~`/etc/wlan_rfhwdata`~~ | — | — | **deliberately not shipped.** Stock wrote it onto flash-backed `/etc`; we read the mtd straight into memory |
| ~~`/tmp/rfhw.sh`~~ | — | — | **deliberately not shipped.** An executable generated at boot and `chmod +x`'d is worse than a function call |

Ordering requirements, because this is the part that bites:

1. `kmod-rtl8192cd` is `AutoProbe`d (was `AutoLoad,60` on 4.14 — with no
   mac80211 PCI card to dodge, the priority game is gone), so the netdev
   exists before `/etc/init.d/network` starts.
2. The **hotplug hook runs at `add`/`register`**, i.e. before anything can
   `ifup`. That is required, not stylistic: xcap is consumed during hardware
   init at interface *open* (§4.2), so applying it after `ifup` is one boot
   late, forever.
3. `rfhw-convert` must be given the **root** interface only. base A measured
   that writing radio-scoped MIBs to a non-root VAP crashes: *"set_mib 'band'
   для НЕ-корневого интерфейса в режиме точки зовёт init_beacon()
   (8192cd_ioctl.c:4175), а у опущенного VAP маяк ещё не собран — Oops в
   init_beacon/update_tim и перезагрузка роутера (поймано на стенде
   06.10.2026)"*.
4. Interface **name is never hardcoded**. `eth_name_conf.txt` and
   `RouterCfm.cfg` disagree about which name means which band (§2), which is
   precisely the class of assumption that broke base B's reference board. Match
   on `/proc/<if>/mib_all` and, if there is ever more than one, on "has a
   14-entry CCK table".
5. BusyBox ash only: no `tr '[:cntrl:]'` (base B's RD05_WLAN.md documents a
   boot-blocking self-inflicted wound from exactly that —
   `BUSYBOX_CONFIG_FEATURE_TR_CLASSES` is off by default, so `tr -d '[:cntrl:]'`
   deletes the literal letters n/t/r and mangles every SSID into `INVALID_SSID`).
   `rfhw-convert` uses `tr '\0' '\n'` and `tr -d ' \r'` for the same reason.

## 7. Open items in this file

* Whether the factory RF-test station also burned the **on-die efuse**
  (`TD_RF_TSFLAG` is set on our unit, and `ther_trim_efuse()` wants efuse
  offset `0x50`). If it did, adding `-DCONFIG_RF_KFREE_SUPPORT
  -DCONFIG_SOC_ENABLE_EFUSE` recovers a thermal trim and a k-free power trim
  that the current build ignores — strictly better RF, one flag, but it reads
  efuse at init and must be watched on a unit whose efuse is blank
  (`val == 0xFF` → `ther_trim_val = 0`, which the code already handles).
* `pwrdiffHT40_2S` (`HW_WLAN1_TX_POWER_DIFF_HT40_2S`) is **absent** on our
  unit; the converter skips empty keys, so it keeps the driver default. Only a
  40 MHz sweep can say whether that is right.
* The value encoding of `xcap` (`HW_WLAN1_11N_XCAP=45` on our unit — decimal
  45 = 0x2d, inside the `0 < xcap < 0x3f` window; as hex 0x45 it would be
  **rejected** by that same check). `rfhw-convert` passes the string through
  verbatim exactly as stock did, so the driver's `INT_T` parser decides; but
  this is a two-way door and the verification in §4 must be done by reading
  the value back, not by trusting the pass-through.

## Which apmib keys are per-unit, and which never reach the driver at all

Compared this unit's CFG apmib (26 `HW_*` keys) with the vendor's shipped
default, `/etc_ro/wlan/.wlan_rfhw.cfg` (49 keys):

| | count | what |
|---|---|---|
| only in CFG | 5 | `HW_NIC0_ADDR`, `HW_WLAN{0,1}_WLAN_ADDR`, `HW_WLAN{0,1}_WSC_PIN` — the per-unit identity |
| in both, same value | 5 | board constants |
| in both, **different** value | 16 | the actual calibration: `HW_WLAN1_11N_XCAP`, `_THER`, `_THER_2`, the `TX_POWER_*` tables, TSSI and RX gain-gap entries |
| only in the default file | 28 | `HW_RFE_TYPE_WLAN{0,1}`, `HW_RX_GAINGAP_*`, `HW_5G_BW40_1S_TSSI_*`, `HW_THERMAL_*` … |

Two consequences:

* `rfhw-convert` is right to read **CFG only**. The per-unit values it needs are
  all there, and the 16 differing entries are exactly the ones a factory
  calibration writes; taking them from the default file would undo the unit's
  calibration.
* `HW_RFE_TYPE_WLAN1` is **not** in this unit's CFG. The vendor's boot only copies
  the default file when the apmib reads blank (`grep HW_XCAP_WLAN0_AX`), and this
  one is not blank, so the RFE type never reaches the driver on this unit and it
  runs on its built-in default — which is what our port does too, since we do not
  read the key either. That is consistency with stock, not an omission: if a
  later unit *does* carry an RFE override, adding `rfe_type` to `rfhw-convert`
  is a one-line change against a value already in the apmib.
