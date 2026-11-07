# What adding RTL8832BR to `rtw89` would actually take

Basis: upstream `drivers/net/wireless/realtek/rtw89/` file listing,
`rtw8852be.c`, `rtw8851be.c`, `rtw8852ae.c` and the full `rtw8852b.c`
`rtw89_chip_info` (fetched from torvalds/linux master 2026-11-07), plus
`linux-firmware/rtw89/` directory listing from the same date. Local corroboration
of "no 8832" in `port/FACTS.md` §6.

## 1. There is no 8832 in the driver today — and no ID to match ours

Upstream chip/bus files: `rtw8851b[e,u]`, `rtw8852a[e,u]`, `rtw8852b[e,t,te,u]`,
`rtw8852c[e,u]`, `rtw8922a[e,u]`, `rtw8922d[e]`. Firmware dir:
`rtw8851b_fw*, rtw8852a_fw*, rtw8852b_fw*, rtw8852bt_fw*, rtw8852c_fw*,
rtw8922a_fw*, rtw8922d_fw*, rtw8922ds_fw*, board_elm.bin`. **No 8832 in either.**

PCI ID table shape (one table per *variant* file, not one global table):

```c
static const struct pci_device_id rtw89_8852be_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0xb852), .driver_data = &rtw89_8852be_info },
	{ PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0xb85b), .driver_data = &rtw89_8852be_info },
	{},
};
static struct pci_driver rtw89_8852be_driver = {
	.name = "rtw89_8852be", .id_table = ..., .probe = rtw89_pci_probe,
	.remove = rtw89_pci_remove, .shutdown = rtw89_pci_shutdown,
	.driver.pm = &rtw89_pm_ops, .err_handler = &rtw89_pci_err_handler,
};
module_pci_driver(rtw89_8852be_driver);
```

Known-good IDs measured from those files: `0x8852`, `0xa85a` (8852AE),
`0xb852`, `0xb85b` (8852BE), `0xb851` (8851BE). **`0xb832` is in none of them.**

**Answer to "would rtw89 even attach to our card?"** No, and not "almost":
`10ec:b832` matches no table, so no `pci_driver` claims the device, `probe` is
never entered, and there is no fallback-by-class. Adding the ID is literally
two lines — and immediately useless, because `rtw89_pci_probe()` dereferences
`driver_data → rtw89_driver_info → chip`, which for 8832 does not exist. Do not
let anyone "test" rtw89 by force-binding: `echo 10ec b832 > .../new_driver`
would fault on a NULL chip_info.

Note also: `10ec:b814` (the ID the sibling Tenda AC23 DTS declares) is the
vendor SDK's **RTL8852A** label (seen in the G6 table removed by `/tmp/rebase-base`
patch 100: `{PCI_DEVICE(0x10ec,0xB814), .driver_data = RTL8852A}`) — and rtw89
does **not** use `0xb814` for 8852A either (it uses `0x8852`/`0xa85a`). So
`b814` is not a back door into rtw89's 8852A support; it would need its own
quirk entry. (Open question: whether Realtek reused `b814` for an RTL8832BR
package variant on some boards — only answerable from a PCI scan on a live
8832BR, which is exactly what `skd-attempt/files/8832-bringup-diag` prints.)

## 2. Minimum code surface for a new chip

Everything below is *per chip*, and the numbers are not estimates — they are
the shape of the 8852B files we just read:

| piece | file(s) | size/effort | what must be known about 8832BR that nobody has published |
|---|---|---|---|
| chip descriptor | `rtw8832.c` | ~2000 lines; `struct rtw89_chip_info` has **~250 initialisers** | register base sets (`rf_base_addr {0xe000,0xf000}` for 8852B), `fifo_size`, `dle_mem`/`hfc_param_ini` tables, `acam/bcam/scam/bacam_num`, efuse sizes (`physical_efuse_size 1216`, `phycap_addr 0x580`), `support_bands`, `txpwr_factor_*`, `*_regs` structs (imr, dig, edcca, pmac, page, wow, rrsr, rfkill), h2c/c2h reg triplets, `ps_mode_supported` |
| chip ops | in `rtw8832.c` | ~40 function pointers | `pwr_on_func`/`pwr_off_func` (a ~120-line XTAL-SI/SPS power-sequence ritual for 8852B — chip-specific and the usual reason new chips don't come up), `bb_sethw`, `set_channel(+help)`, `read/write_rf`, `rfk_init/channel/track`, `tssi/dpk/iqk/rck/dack`, `power_trim`, `set_txpwr*`, `get_thermal`, `query_ppdu`, `convert_rpl_to_rssi`, `read_efuse`, `read_phycap` |
| BB/RF register init tables | `rtw8832_table.c` | **hundreds of KB to ~1.7 MB** (8852A's is 1 746 009 bytes) | the actual PHY init sequences. In the vendor world these are the `PHY_REG.txt`/`RadioA/B.txt`/`PHY_REG_GAIN.txt` files we already have for RFE50 — a plausible transcription source, but the vendor files are *runtime-loadable text*, not rtw89's `{addr,mask,val}` tables |
| RF-calibration (RFK) | `rtw8832_rfk.c` + `rtw8832_rfk_table.c` | ~120 KB + ~70 KB for 8852A | IQK/TSSI-TRK/DPK/RCK/DACK/FC/CCK-PK thresholds per channel group |
| coex | `coex.c` shared + `btc_*` fields in chip_info | medium | 8832BR here has **no BT**; vendor tree still compiles BTC in (`rtw_hal_btc_get_efuse_info`, `rtw_hal_btc_process_c2h`, `_btc_gpio` strings in our stock kernel). Need to confirm whether the 5 GHz/2.4 GHz pair (separate drivers, separate silicon) needs any coex notification at all, or `bt_solo=1`/no-BTC. Unknown, low risk. |
| bus glue | `rtw8832e.c` | ~150 lines | `rtw89_pci_info` quirk set — see §3 |
| firmware | `linux-firmware` | blocked | see §4 |
| Kconfig/Makefile/symbol export | `rtw89/Kconfig`, `Makefile`, `Makefile.kernel`? | trivial | `CONFIG_RTW89_8832E` etc. |

Structural facts that help: the 8832BR is an **AX-generation** chip, not BE.
Evidence from our own stock kernel (`/tmp/work3/kernel.lzma.out`): only
`mac_ax_*` / `hal_mac_ax_*` HAL symbols exist (`mac_ax_parse_ppdu_sts`,
`mac_ax_send_fw_snd`, `mac_ax_set_mu_fix_mode`, …) and **zero** `mac_be_*`
strings. So a port would sit on `rtw89_mac_gen_ax`/`rtw89_phy_gen_ax`/
`rtw89_pci_gen_ax`/`rtw89_pci_isr_ax` (the 8852x code path), not on
`mac_be.c`/`pci_be.c`. That is the single most useful thing this analysis
produced: it says the 8832 work is comparable to "second PCIe variant of the
8852A/B family", not to "add 8922 support".

Also chip-specific and easy to miss: RTL8832BR is **5 GHz only**
(`rtw89_chip_info.support_bands = BIT(NL80211_BAND_5GHZ)`; the vendor driver
logs `CONFIG_WLAN0_RFE_TYPE_50` and its band is fixed) — most 8852x code assumes
S0G+S1 dual-band, so `regd`, txpwr limits, and rfk paths need band guards.

## 3. `rtw89_mac` / `rtw89_pci` quirks we would have to set blind

From `rtw8852be.c`/`rtw8852ae.c`, the per-chip `rtw89_pci_info` is ~45 fields:
`txbd_trunc_mode/rxbd_trunc_mode/rxbd_mode/tag_mode/multi_tag_num`,
`tx_burst=MAC_AX_TX_BURST_2048B`, `rx_burst=MAC_AX_RX_BURST_128B`,
`wd_dma_{idle,act}_intvl`, `lbc_en/lbc_tmr`, `autok_en`, `io_rcy_en/io_rcy_tmr`,
`rx_ring_eq_is_full`, `check_rx_tag`, `no_rxbd_fs`, `group_bd_addr`,
`rpp_fmt_size`, plus the register picks `init_cfg_reg`,
`txhci_en_bit/rxhci_en_bit/rxbd_mode_bit`, `exp_ctrl_reg`, `max_tag_num_mask`,
`rxbd_rwptr_clr_reg`, `txbd_rwptr_clr2_reg`, `dma_io_stop/dma_stop1/dma_stop2`,
`dma_busy{1,2,3}_reg`, `rpwm_addr/cpwm_addr/mit_addr/wp_sel_addr`,
`tx_dma_ch_mask`, `dma_addr_set`, `bd_ram_table`, and the function picks
`ltr_set/fill_txaddr_info/parse_rpp/config_intr_mask/enable_intr/disable_intr/recognize_intrs`,
`ssid_quirks`.

For 8832BR each of those is a register-level question ("does the chip have
CH4..7/CH10..11 DMA channels? single or dual BD RAM? v0 or v1 RPP format?").
`rtw89_mac` adds another set (`rtw89_mac_size` table entries, DLE memory
layouts, page/queue allocation, `hfc_preccfg`). Everything about 8832 in this
section is **unknown and unpublished**; the only source is the vendor HAL, i.e.
transcription. Note also that rtw89's LTR/ASPM/clkreq code
(`rtw89_pci_is_dac_compatible_bridge`, `pci_upstream_bridge()`,
`RTW89_PCIE_LTR_*`) assumes a normal PCIe bridge — on this SoC the root port is
the RTL8197F's own, and both rtw89 and the SKD will hit ASPM/L1/clkreq paths
that nobody has validated on RTL8197F. That is a shared risk with route (a), not
an rtw89-specific one, but rtw89 exercises far more of it (L1SS, PM, WoWLAN).

## 4. Firmware: naming, and the redistribution judgement

Naming mechanism (measured in `rtw8852b.c`):

```c
#define RTW8852B_FW_BASENAME "rtw89/rtw8852b_fw"
#define RTW8852B_FW_FORMAT_MAX 2
#define RTW8852B_MODULE_FIRMWARE RTW89_GEN_MODULE_FWNAME(RTW8852B_FW_BASENAME, ...)
.chip_id = RTL8852B,
.fw_def = { .fw_basename = RTW8852B_FW_BASENAME, .fw_format_max = RTW8852B_FW_FORMAT_MAX, .fw_b_aid = 0 },
MODULE_FIRMWARE(RTW8852B_MODULE_FIRMWARE);
```

i.e. basename + `_max` format probes, resolving to the actual
`linux-firmware` files `rtw89/rtw8852b_fw.bin`, `rtw89/rtw8852b_fw-1.bin`,
`rtw89/rtw8852b_fw-2.bin` (sizes 1.0–1.5 MB). (The older
`rtw89/RTW8852B_FW_<date>_<ver>.bin` scheme is gone.) An 8832 entry would
therefore require `rtw89/rtw8832_fw.bin` (+ format variants, + eventually
`rtw89/rtw8832_txpwr_limit*.txt` / `board_elm.bin` entries).

Is the stock blob reusable?

* The 8832BR image exists to us **only as a compiled-in C array inside vendor
  GPL tarballs** (the G6 tree; `CONFIG_FILE_FWIMG=n`; our stock rootfs has no
  `/lib/firmware` at all). Extracting it is mechanical (dump the array out of
  the G6 source, or out of our kernel image at the offset the array occupies).
* Legally: the tarball's *source* is GPL-2.0; firmware arrays in Realtek
  drops ship under Realtek's own header, and linux-firmware's rule is an
  explicit per-file `WHENCE` notice plus, in practice, Realtek publishing the
  file. **Judgement: extract-and-ship-with-our-own-package = defensible for an
  out-of-tree port (this repo already treats vendor RF data as not
  redistributable — see `skd-attempt/files/rfe50-manifest.sha256`); contributing
  it to linux-firmware = must come from Realtek, not from us.** So route (b) is
  gated on Realtek shipping `rtw8832_fw`, which they have shown no sign of
  doing (8832BR is a router-embedded part with no M.2/PCIe card retail
  presence, so no `lspci` incentive for anyone upstream).
* Version check that will bite: rtw89 validates FW by `rtw89_fw_check_page_cnt`
  / needed-elements (`needed_fw_elms`, `fw_blacklist`, `try_ce_fw`). A blob
  extracted from a 4.4-era vendor tree may not satisfy AX-gen element checks —
  more transcription work.

## 5. Verdict

Route (b) = "write and upstream a new chip", gated on **unpublished register
data** and on **firmware we cannot legitimately publish**. Estimated cost,
based on the file sizes in §2 (transcribing vendor AX-gen tables into rtw89
structs) plus §4's blocker: several person-months *after* the vendor driver is
already working, and it cannot be the thing that makes 5 GHz exist for this
port. Its value here is as the spec for upstreaming **after** route (a) proves
the board-level plumbing (identity BAR2, INTx on CPU5, RFE50 files) — which is
also the part of route (b) that is genuinely reusable knowledge for rtw89
users on RTL8197F.
