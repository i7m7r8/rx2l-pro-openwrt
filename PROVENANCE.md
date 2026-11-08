# Provenance and licensing

## What came from where

| piece | origin | licence |
|---|---|---|
| base tree | `ggbruno/openwrt` @ `8a0ccb93f3431bcf8f5c5d03d4acc2c8e442de67` (branch `Realtek`) — the only OpenWrt tree with an `rtl8197f` subtarget | GPLv2 |
| device support overlay, `cvimg` host tool, `mtdsplit_cvimg.c`, the RTL8192CD vendor driver, mac80211 patches, build container recipe | [ADCDS/openwrt-dlink-dir842-r1](https://github.com/ADCDS/openwrt-dlink-dir842-r1) (`files/`, `Dockerfile`, `feeds.conf`, `seed-m5.config`) | GPLv2; the vendored `rtl8192cd` driver carries its own Realtek header |
| RX2L Pro board layer: `RX2LPRO.dts`, `Device/RX2LPRO`, the `cr6c` recipes, `tenda-wrap.c`, `check-cr6c.py`, the `mtdsplit` offset fix, MAC/flash/RAM/console facts, this repo's README and notes | derived here, from this device's own dumps | GPLv2 |

`files/` is carried as a path-preserving overlay: `build.sh` copies it onto the
pinned base tree, exactly as the reference port does. Where the reference and
this port differ, the divergence is commented in place (the `cr6c` recipe vs
`cr6b`, 115200 vs 38400, the rootfs-offset fix, the absence of a D-Link
keyed-MD5 trailer).

## Vendor firmware

No vendor firmware, no decrypted vendor image, and no device-specific secret
is stored in this repository. The reverse-engineering results in `notes/` are
descriptions of formats and algorithms, and the numbers quoted there (offsets,
field values, checksums) are reproducible from any legitimately obtained stock
upgrade file.

The per-unit MAC addresses of the development device are elided in the docs and
never hard-coded in a script, because the stock console root password is
derived from the LAN MAC.

## Statements this repo makes that are NOT vendor claims

* WAN MAC = LAN MAC + 1 — our choice, because the vendor stores no `wan_mac`.
* The `cr6c` container's checksum format, header fields and rootfs geometry —
  established by reproducing them byte-exactly on real images, not by reading
  documentation.
* `start_addr 0x82000000` in our images — our loader's link address, not the
  vendor's `0x80d00000`. The stock stage-1 takes the value from the header and
  applies no bound check (`port/02-image-container/loader/LOADER.md` §6); the
  32 MiB window is what the initramfs image needs.
* Anything marked `TODO(port-facts)` in the DTS and `seed.config` is a known
  unknown, deliberately not guessed.
