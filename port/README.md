# port/ — the 6.18 / latest-OpenWrt rebase, in flight

`FACTS.md` is the shared brief: measured facts and ground rules every track
reads before it writes anything. Everything else here is **staged** work from
the parallel tracks — nothing in this directory is wired into the build until
the orchestrator integrates it and CI is green.

| dir | track | lands in |
|---|---|---|
| `01-base-build/` | build.sh, feeds, Dockerfile, CI, seed.config audit for the new base | repo root |
| `02-image-container/` | cr6c/cvimg + Tenda wrapper + loader + rootfs split | `files/target/linux/**/image`, `tools/`, `tests/` |
| `03-dts-switch/` | `RX2LPRO.dts`, switch node, GPIO numbering | `files/target/linux/**/dts` |
| `04-eth-hwnat/` | PHYLINK ethernet + hardware NAT/flow offload | driver + offload package |
| `05-wifi-2g/` | on-SoC WMAC (rtl8192cd HAL) on 6.18 + RF calibration | `package/`, uci-defaults |
| `06-wifi-5g-8832br/` | Wi-Fi 6 client: route decision and first attempt | research + package skeleton |
| `07-usersland/` | base-files, sysupgrade, modern LuCI/firewall4 package set | `files/`, `seed.config` |

Rules of the road:

* A track writes **only** inside its own directory. Cross-cutting changes are
  proposed in its `REPORT.md` under "Needs from elsewhere".
* No per-unit MACs, serials, WPS PINs or the derived telnet password, ever.
* No builds on a workstation: images are built by GitHub Actions in this repo.
* The device stays read-only for us — verification happens on dumped flash
  images, in CI, or by RAM-boot, never by writing mtd partitions.
