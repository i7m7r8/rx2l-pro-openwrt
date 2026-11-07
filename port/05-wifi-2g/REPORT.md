# REPORT — track 05, 2.4 GHz (RTL8197F on-SoC WMAC)

## What I decided, in one paragraph

Adopt **iptime A2004MU's out-of-tree `package/kernel/rtl8192cd`** (Realtek
8197F HAL, cfg80211, Linux 6.18, hardware-tested on that board) as the driver;
take `rtlmib` (the `iwpriv`→`SIOCDEVPRIVATE` shim) and the MIB-name evidence
from **rx23302**; use **tobyw121 base B** only as the calibration-format
reference. Reject our own 4.14 in-tree copy outright — it is not portable, it
is 1117 files/792k lines with three hard 6.x breakages, and its provenance is a
D-Link DIR-842 R1 drop, i.e. the wrong chip.

Deliverables: `route-decision.md`, `calibration.md`, `uci-wireless.txt`,
`package/` (7 staged files, see `package/README-layout.md`).

## Blockers (in the order they will bite)

**B1 — `PDE_DATA` in the adopted driver, unresolved by inspection (build risk).**
`8192cd_proc.c:88`'s own `void *PDE_DATA()` is correctly guarded out on ≥3.10,
and `osdep_service_linux.h:145/:257` define `__LINUX_3_10__`/`__LINUX_5_6__`
from `LINUX_VERSION_CODE`, so `proc_ops` is selected — but the *replacement*
for `PDE_DATA()` (renamed to `pde_data()` in 5.17) is not visible in the
driver tree; it must come from `-include backport/backport.h`. iptime ships
exactly one file I would expect to hold it (`rtl8197f_compat.h`, 18 lines, and
it does not) and I could not grep the backport headers without building.
**Mitigation, already written into the staged Makefile as a comment:** if CI
shows `implicit declaration of function 'PDE_DATA'`, add
`-DPDE_DATA\(inode\)=pde_data\(inode\)` to `EXTRA_CFLAGS`. One line, no driver
patch. Treat the first CI build as the experiment, per README risk #7.

**B2 — the driver does not probe from DT; only iptime's tree tells the truth
about how the WMAC attaches.** The integrated MAC comes up from a hard-coded
entry in the driver's own device table (`TYPE_EMBEDDED`, base
`0xb8640000`/uncached alias of `0x18640000`, IRQ 6 — `rtl8197f_compat.h:6-7`)
called directly from `rtl8192cd_init_one()` (`8192cd_osdep.c:18916`). The DTS
node is consumed **only** by `of_find_compatible_node()` inside the
calibration loader. Consequences the orchestrator must not lose: (i) `status =
"disabled"` will *not* stop the radio; (ii) `option path '18640000.wifi'` in
UCI only works if cfg80211 registers a wiphy under that device; (iii) the IRQ
number is not DT-driven and must not be assumed to match our base's
`cpuintc`. Verify with `grep -i irq /proc/interrupts` on first boot.

**B3 — our board's calibration is in a format the adopted driver cannot read,
so calibration is userspace work we own.** No packed H601 blob exists on this
device: `H601` occurs **zero** times across all seven flash dumps (searched
byte-wise), and the apmib is a NUL-separated `KEY=VALUE` text store. iptime's
loader requires `len == 0x640` from an nvmem cell. My route is a new
`/usr/sbin/rfhw-convert` that reproduces stock's `dd → rfhw_convert →
/tmp/rfhw.sh → iwpriv set_mib` chain against the driver's own MIB table. It is
**new code, unrunnable here** (no builds, no device). Until it is executed on
hardware it is unproven, and I would ship it alongside a one-shot
`rfhw-convert --dump wlan1` in the boot log so the first person at the console
can see what it *would* have written.

**B4 — FACTS.md §6 is wrong on one number and I did not silently copy it.**
"8192 B of CFG read from flash offset 8" — stock's `count=8192 ibs=8 skip=1`
is 8192 blocks **of 8 bytes** = a 64 KiB read from offset 8, i.e. the whole
mtd2 minus its header. Anyone implementing the reader from FACTS alone will
read 8 KiB and miss most of the keys. **Please correct FACTS §6.**

**B5 — RFE type is not settled from dumps** (`0` vs `1+EXT_PA+EXT_LNA`; the
three reference boards disagree and our apmib has no `HW_RFE_TYPE_*` key).
Staged as `RX2LPRO_RFE_TYPE?=0`, one `make` variable to flip; the boot log
prints `RFE TYPE =%d`, which is how to settle it. Details and the evidence
table: calibration.md §5.

**B6 — `ifwireless` is unresolved as a *question*, not as a need.** No tree we
have uses it (`grep -rn ifwireless` across both 6.x bases and our repo: zero
hits), so I shipped `wifi-device` + `wifi-iface`/`default_radio0`. If the
rebase target is new enough to require `ifwireless` for key/`ieee80211w`, the
only file that changes is the staged uci-defaults. Needs the images/target
agent's kernel+`netifd`/`mac80211` revision to close.

**Not blockers, but do not forget:** `option he` must stay unset (driver C has
`ht_cap` for 2 GHz and `vht_cap` for 5 GHz only — no `he_cap` anywhere);
`/etc/Wireless → /tmp` symlink is still required (`RTL8192CD.dat` path is
alive at `8192cd_comapi.c:3614`); radio-scoped `set_mib` must only go to the
**root** interface (rx23302 measured an `init_beacon()` reboot loop doing
otherwise); BusyBox has `FEATURE_TR_CLASSES` off, so `tr -d '[:cntrl:]'` is a
boot-blocker (base B's RD05 `INVALID_SSID` incident).

## Minimum for 2.4 GHz to be up on first boot — README checklist

Proposed replacement/expansion of README risk #7 ("2.4 GHz driver actually
builds"). Each line is checkable from the serial console of an
**initramfs** image (nothing flashed), in this order. A line failing is a
specific, named failure — that is the point.

```
FIRST-BOOT CHECKLIST — 2.4 GHz (fallback access path; must be green before any flash)

 1. dmesg | grep -i 'rtl8192cd\|8197F'
    -> module loaded, "RFE TYPE =N" printed. NOTHING printed = kmod-rtl8192cd is
       not in the image (seed.config lost it silently at defconfig).
    STOP: if absent, 2.4 GHz is gone and Ethernet is unproven -> UART only.

 2. ls /proc/wlan*/mib_all
    -> at least one hit. This is the identity test for "the vendor HAL owns a
       netdev"; it also tells you WHICH name it got (do not assume wlan0).

 3. iw list | head -40     (or: ls /sys/class/ieee80211/)
    -> a phy exists and lists HT on 2.4 GHz. Empty while #2 succeeded = wiphy
       not registered: use the WEXT config form (uci-wireless.txt §4) instead
       of assuming `wifi detect` will find the radio. It will not.

 4. cat /proc/mtd | grep -w CFG   AND   hexdump -C -s 8 -n 32 /dev/mtd2
    -> the apmib partition is visible and starts with keys, not 0xff.
       If mtd2 is missing, calibration can never be applied (see #5).

 5. rfhw-convert --dump <the-iface-from-#2>
    -> lines `set_mib xcap=NN`, `pwrlevelCCK_A=<28 hex>` ... Non-empty.
       Empty output = no calibration: the AP still comes up and still
       ASSOCIATES, but on compiled-in tables. This is the silent one; do not
       accept "wifi works" as proof. Then:
         rtlmib <if> get xcap   (or /proc/<if>/mib_rf) -> not 0xff/0.

 6. ip -br l show dev <if> | awk '{print $3}'   (before any ifup)
    -> NOT 00:e0:4c:cc:dd:01. That literal is the driver's fake MAC fallback
       (8192cd_cfg80211.c:103, `rtk_fake_addr`); any two units in a room will
       share it. If you see it, #4/#5 failed.

 7. iw info <if> frequency; iw reg get
    -> channel/frequency plan matches the country you intend. Driver has its own
       100-country table; `option country` may be cosmetic. Unresolved (B6 note).

 8. uci show wireless | head -20
    -> exactly ONE wifi-device, type matches what #3 proved, `disabled 0`,
       NO 5 GHz section. A seeded 5 GHz radio with no driver makes netifd fail
       the whole wireless bring-up, 2.4 GHz included (our own seed.config says
       this and it is still true).

 9. wifi; sleep 10; ifconfig <if>
    -> flags include UP,BROADCAST,RUNNING. Then:
       logread -e 'rtl8192cd\|hostapd\|Unsupported' | tail -30
    -> no `Unsupported` / `NOT_RTK_BSP` / Oops / `init_beacon` trace.

10. From a client: scan for "RX2L-2G", associate, `odhcp6c`/dhclient, ping the
    router. Then the ONLY test that calibration is real: walk 5 m away and
    watch `iwinfo <if> assoclist` signal + whether throughput collapses.
    A unit that is fine at 1 m and useless at 5 m is an untrimmed crystal, not
    a bad driver.
```

Rules to write into README alongside it:
* Interface names are **never** hardcoded — #2 decides them.
* Radio-scoped settings go to the **root** interface only.
* No `option he` on this radio; no VHT htmode on 2.4 GHz.
* No per-unit MAC/serial/PIN in any staged file; derive LAN+4 at runtime.
* Nothing gets flashed until #1–#9 are green on an initramfs boot.

## What I did NOT do, deliberately

* No 5 GHz work (RTL8832BR stays its own track; a `rtw89`-style fix does not
  exist for it and seeding it is an active harm — see #8).
* Did not touch `seed.config`, `build.sh`, `files/`, or another agent's dir.
  The proposed edits are: `seed.config` gains `CONFIG_PACKAGE_kmod-rtl8192cd=y`
  + `CONFIG_PACKAGE_rtlmib=y` and drops the 4.14 `CONFIG_RTL8192CD=m` kernel
  symbol; `image/Makefile`'s `RX2LPRO DEVICE_PACKAGES` swaps
  `kmod-rtl8192cd` (it will resolve to the new package) and gains `rtlmib`;
  `files/target/linux/realtek/files-4.14/drivers/net/wireless/rtl8192cd/` and
  `modules.mk`'s `KernelPackage/rtl8192cd` become deletable once CI is green.
* Did not commit or push; nothing built.

## Word on confidence

Route decision and calibration analysis: high — every claim above is from a
file I opened in this session, and the two decisive ones (no `H601` anywhere in
flash; `xcap`/`pwrlevel*`/`rfe` are driver MIB keys) are things a rebuild can
contradict but not argue with. The staged rfhw-convert and the rewritten
uci-defaults: written but **unexecuted**, and B1 is a genuine coin-flip that
only CI settles.

## Verification pass on the 6.18 base (2026-11-07 evening)

Checked, against the pinned overlay tree, that the pieces this track depends on
are actually present and agree with each other. Everything below was read, not
assumed — and two of the five would have produced a radio that never appears.

| link in the chain | evidence | verdict |
|---|---|---|
| HAL compiled | `config-6.18` sets `WLAN_HAL=y`, `WLAN_HAL_88XX=y`, `WLAN_HAL_8197F=y`, `RTL8192FE_RX23302=y`, `WEXT_*` | ✔ |
| HAL wired into the kernel build | `patches-6.18/737-wlan-rtl8192fe.patch` adds `obj-$(CONFIG_RTL8192FE_RX23302) += rtl8192fe/` to `drivers/net/wireless/Makefile` — the config symbol is real, unlike the switch driver's | ✔ |
| HAL source present | `files-6.18/drivers/net/wireless/rtl8192fe/` | ✔ |
| netifd can find the netdev by identity | HAL does `proc_mkdir(dev->name, NULL)` (`8192cd_proc.c:10320`) and creates `mib_all` under it, which is exactly what `rtl_find_phy()` probes (`/proc/<if>/mib_all`) | ✔ |
| the config file the handler writes is the one the HAL reads | handler writes `/etc/Wireless/RTL8192CD.dat`; HAL's `CFG_FILE_PATH` is the same string (`8192cd_comapi.c:3614`) | ✔ |

Also added to the shipped handler: `drv_rtl8192cd_init_vlan_config()` and
`drv_rtl8192cd_init_station_config()` as no-ops. netifd calls them by name for
vlan/station stanzas; the platform's own handler for this SoC defines both, ours
did not, so such a stanza would have died on an undefined function.

Still unverifiable without hardware (unchanged): whether our uci option names
(`band` as a bitmask, `htmode`, `led_type`) mean the same thing to the 6.18 HAL
as they did to the 4.14 vendor driver. The two handlers in play — ours and the
platform's 527-line one — read different option sets, and this tree keeps ours
because it is paired with `uci-defaults/12_wireless-2g-rx2lpro`. Deciding
between them needs one boot with `wifi up` and a look at the radio.
