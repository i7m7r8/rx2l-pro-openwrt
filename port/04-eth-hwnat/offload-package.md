# offload-package.md — what has to be installed/enabled for hwNAT to actually kick in

Two different questions get conflated under "offload" on current OpenWrt, and both have
to be yes:

* **software** flow offload — mainline `nf_flow_table` reached via `nft` `flowtable`;
  measured **850–880 Mbit/s** on the RX23302 (same RTL8197F, 999 MHz) —
  `05_enable_flow_offloading`: *"По умолчанию — программный офлоад (850–880 Мбит/с,
  проверено)"*.
* **hardware** NAT — the RTL8197F's own NAPT engine; measured **926–929 Mbit/s at
  90–100 % CPU idle** on the same box (`rtl819x-hwnat` header comment).

Default for our image = software on, hardware off (`flow_offloading_hw='0'`), exactly as
base A ships it, and for a reason worth repeating: *"Отказ хотя бы на одном [устройстве]
роняет транзакцию nft, и fw4 уходит в deny-all (проверено: связь пропадала целиком)"* —
in fw4 a refused offload bind is not a warning, it is a **dead box**. Keep that default
until the first-boot checklist passes.

---

## 1. Kernel side

### 1.1 Built into the image by the target (already true)

`port/01-base-build`'s overlay `target/linux/rtl819x/rtl8197f/config-6.18` (250 lines)
already sets — verified by reading the file:

```
CONFIG_NET_RTL819X=y            (line 225)
CONFIG_SWCONFIG=y               (208)   CONFIG_SWCONFIG_LEDS=y (209)
CONFIG_PHYLINK=y                (229)   CONFIG_PHYLIB=y (179)
CONFIG_SOC_RTL8197F=y           (201)   CONFIG_NET_DSA is not set (226)
CONFIG_NF_CONNTRACK=m           (152)   CONFIG_NF_CONNTRACK_MARK=y (153)
CONFIG_NF_NAT=m                 (159)   CONFIG_NF_NAT_REDIRECT=y (160)
# CONFIG_PCI is not set         (244)
```

It contains **no** `*FLOW*`, `*CLS*`, `*QDISC*`, `BRIDGE`, `8021Q` or `NFT` symbol at all.
Those come from kmod packages, which is the whole risk: see 1.2.

### 1.2 Symbols the datapath needs that are NOT in the target config

| symbol | who needs it | how it must be set | why it can silently fail |
|---|---|---|---|
| `CONFIG_NET_CLS_ACT` | `rtl819x_hwnat.c` includes `<net/pkt_cls.h>`; `TC_SETUP_FT`, `struct flow_block_offload`, `tcf_block` all exist only under `IS_ENABLED(CONFIG_NET_CLS_ACT)` | **`=y` (built-in)**, because `NET_RTL819X` is `=y` | without it the driver does not *compile*, so CI catches this one |
| `CONFIG_NET_FLOW_OFFLOAD` | `net/flow_offload.c` exports `flow_indr_dev_register()`, `flow_indr_block_cb_alloc()`, `flow_block_cb_lookup()` — called by `hwnat_init()` (`rtl819x_hwnat.c:653`) and by the BIND path | **`=y` (bool)**. It is a non-visible bool `select`ed by `NF_FLOW_TABLE`, so if `NF_FLOW_TABLE=m` the object must still land in `vmlinux` | a built-in driver referencing modularised flow-offload symbols = **link error at `make target/linux/compile`**. Check `grep '^CONFIG_NET_FLOW_OFFLOAD' .config` → must be `=y` |
| `CONFIG_NF_FLOW_TABLE{,_INET,_IPV4,_IPV6}` | the nft `flowtable` object itself | `=m` is fine (pulled by `kmod-nf-flow`) | if absent, `nft` rejects `flowtable` and fw4 goes deny-all |
| `CONFIG_NFT_FLOW_OFFLOAD` | the `flowtable` → `ndo_setup_tc` glue, i.e. `nft_flow_offload.ko` | `=m` via `kmod-nft-offload` | the *silkiest* failure in the whole track: uci accepts `flow_offloading=1`, nft loads the ruleset, **nothing is offloaded** |
| `CONFIG_BRIDGE` / `CONFIG_BRIDGE_VLAN_FILTERING` | `br-lan`, and the 8021Q VIDs `eth0.8`/`eth0.9` | `=y`/`=m` per master defaults | `eth0.8` missing ⇒ no WAN device ⇒ `is_router()` false ⇒ gate never arms |
| `CONFIG_NET_VENDOR_REALTEK`/`REALTEK` | `NET_RTL819X` `depends on REALTEK` (patch 720) | must be `y` in the `.config` or the Kconfig line is dropped at `defconfig` | silent: defconfig just forgets `CONFIG_NET_RTL819X=y` |

**Not verifiable on this machine** (no full kernel checkout, no builds here). Exact
checks to run in the GitHub Actions job, in this order, before any image is trusted:

```sh
grep -E '^(CONFIG_NET_CLS_ACT|CONFIG_NET_FLOW_OFFLOAD|CONFIG_NF_FLOW_TABLE|CONFIG_NFT_FLOW_OFFLOAD|CONFIG_NET_RTL819X)=' .config
# on the built image, not the config:
zcat build_dir/target-*/linux-*/linux-*/.../config 2>/dev/null | grep FLOW
ls /sys/module/rtl819x/parameters/            # hwnat must be 0644-present
lsmod | grep -E 'nf_flow_table|nft_flow_offload'
```

### 1.3 Packages (`07-usersland` owns `seed.config.new`; these are the eth-track lines)

```
CONFIG_PACKAGE_firewall4=y              # NOT firewall3/iptables — they must not coexist
CONFIG_PACKAGE_nftables-nojson=y
CONFIG_PACKAGE_kmod-nf-flow=y           # name UNVERIFIED locally, see below
CONFIG_PACKAGE_kmod-nft-offload=y       # name UNVERIFIED locally; this is the one that
                                        # makes flow_offloading=1 mean anything
CONFIG_PACKAGE_kmod-nf-nat=y
CONFIG_PACKAGE_swconfig=y
CONFIG_PACKAGE_kmod-swconfig=y          # may legitimately vanish from .config:
                                        # CONFIG_SWCONFIG=y means it is built in
```

**`kmod-ipt-offload` must NOT be carried forward** (stated by `01`/`07`): it is the
iptables-side symbol on the 4.14 base and iptables is not in this image.
Package names above were read from master's `package/kernel/linux/modules/*.mk` in no
local checkout, so verify with:

```sh
grep -rl 'Define KernelPackage/nft-offload' package/kernel/linux/modules/
grep -rl 'Define KernelPackage/nf-flow'     package/kernel/linux/modules/
```

---

## 2. Userspace side: which interface, exactly

There are **three** candidate control planes and only one is used. Do not add a fourth.

1. **Driver-private netlink / `iwpriv`-style interface — NOT used.** Base A's driver
   exports no netlink family; the 4.14 donor used a downstream
   `ndo_flow_offload` ("там на ядре 4.14 использовался downstream ndo_flow_offload и
   ручное удержание таймаутов" — `rtl819x_hwnat.c` header). That whole path is deleted.
2. **`tc`/`cls_flower` directly — not used by us**, but it is the *same kernel machinery*:
   `FLOW_CLS_REPLACE` / `FLOW_CLS_DESTROY` / `FLOW_CLS_STATS` are what
   `hwnat_setup_block()` receives. (`FLOW_CLS_STATS` is wired to our `lastused`, so the
   ASIC refreshes row aging when the kernel asks — `rtl819x_hwnat.c` header.) The iptime
   donor documents its own rules in `FLOW_CLS_*` terms too, and base B's `rtl8197f_rtknet`
   exposes a `tc-flower` binding that reports "not supported" explicitly.
3. **`nft`/`flow_offload` via firewall4 — THE interface.** fw4 emits

   ```nft
   flowtable ft { hook ingress priority 0; flags offload; devices = { "br-lan", "eth0.8", ... } }
   ```

   `flags offload` is what makes the kernel call `ndo_setup_tc(dev, TC_SETUP_FT, …)` on
   each listed device. Without the flag, no offer is ever made, and **no counter, log or
   error tells you**.

### 2.1 firewall4/nftables interaction — three non-obvious rules

* **Rule 1: a refusal on *any* listed device fails the whole ruleset.** fw4 puts every
  zone port in `ft` (`br-lan`, `eth0.8`, `eth0.9`, `wlan0/1`); `eth0.8`/`eth0.9`/`br-lan`
  are VLAN/bridge devices that have **no** `ndo_setup_tc` of their own. Base A solves this
  with `flow_indr_dev_register()` + `hwnat_indr_cb()`, which **accepts any device**
  (`rtl819x_hwnat.c:609-651`) and lets the ASIC simply miss those flows. Do not "improve"
  that by filtering to `dev == eth0` — that is the deny-all bug.
* **Rule 2: the software `eth0` flowtable snippet and hardware NAT are mutually
  exclusive**, and the reason is a kernel/nft limitation, not a preference.
  `20-rx2lpro-flowtable-eth0.nft` (staged by `07-usersland`, renamed from base A's
  `20-rx23302-flowtable-eth0.nft`) adds bare `eth0` to a flowtable **without**
  `flags offload`. Declaring the same flowtable name twice / mixing flagged and
  unflagged declarations makes `nft` fail with `Not supported` → fw4 deny-all
  (`rtl819x-hwnat` header: *"с `flags offload` объявление без флага роняет nft, и fw4
  уходит в deny-all"*). Hence `/etc/init.d/rtl819x-hwnat` (`START=17`) **deletes the file
  when `flow_offloading_hw=1`, restores it when 0**, and must run **before** firewall
  (`START=17 < 19`) so fw4's first load sees the right snippet set. Keep that snippet's
  `devices = { "eth0" }`: fw4 only puts *zone* devices in `ft`, so offload would
  otherwise start only after the 802.1Q layer — the snippet is worth **766/703 →
  850–876 Mbit/s** measured (`20-rx23302-flowtable-eth0.nft` header).
* **Rule 3: the hardware gate must be applied AFTER the network, the snippet BEFORE the
  firewall.** `rtl819x-hwnat-gate` is `START=95` (> network's 20) because *"router mode
  пишет в свитч PVID/VLAN/MACCR1, а драйвер при ifup перезаписывает их в
  rtl819x_intsw_nastroit(); Включение до ifup давало кривой L2 (проверено 29.10: сеть
  пропадала)"*. Runtime toggle is **not** safe either: `reload_service()` there does a
  live `reboot` when `flow_offloading_hw` flips, because on-the-fly router-mode change
  once "повесило box намертво (большой кадр = wedge; лечится только питанием)". Do not
  replace that reboot with `restart`.

### 2.2 What the gate actually writes

`echo 1 > /proc/rtl819x_router` (router mode) → `echo 1 > /sys/module/rtl819x/parameters/hwnat`
→ `cat /proc/rtl865x_gw >/dev/null` (program the netif/route/nexthop/ACL scaffolding).
All three must be present in our version, plus the warm-up our `dir842-asic` adds
(`driver-port.md` §1.2).

Full proc/sysfs surface the package must not shadow (`proc_create` lines in base A):
`/proc/rtl819x_spa` (ro, per-source-port counters), `/proc/rtl819x_gnezda` (rw, jack
masks), `/proc/rtl819x_router` (rw, router mode), `/proc/rtl819x_hs` (rw, hwNAT rules
dump), `/proc/rtl865x_gw` (ro, `gw_prog`), `/proc/rtl865x_dump`, `/proc/rtl865x_napt`
(NAPT row scan — **the row count is the direct proof hardware NAT is live**),
`/proc/rtl865x_fabric`, `/proc/rtl865x_l2flush` (rw, MAC invalidation).

---

## 3. sysctls and hotplug

**sysctls** (`/etc/sysctl.d/30-hwnat.conf`, keep as-is — already in our tree):

```
net.netfilter.nf_conntrack_acct=1
```

That single line is the cheapest *hardware* proof available: an offloaded flow's packets
never reach the CPU, so `conntrack -L -p tcp --dport 5201` shows **frozen** `bytes=`/`packets=`
while `iperf3` keeps reporting full throughput. Add to the same file only if the soak
proves it needed — do not pre-tune:

```
net.netfilter.nf_conntrack_max=8192      # 128 MiB RAM; 1024 NAPT rows is the real ceiling
net.netfilter.nf_conntrack_tcp_timeout_established=432000  # leave default unless flows age out
```

The ASIC's own timer is separate and shorter: `RTL865X_NAPT_AGING_RELOAD = 0x11` ≈
**102 s of idle before the hardware row self-removes** (`rtl865x_asichal.h:25`). A flow
idle > 102 s silently falls back to software; that is by design and is *not* a failure.

**hotplug**: there is **no** `/etc/hotplug.d/` file in either tree today. WAN-attach
reprogramming is done by `rtl819x-hwnat-gate`'s `procd_add_reload_trigger firewall` plus
the fact that the gate declines flows whose extIP does not match the live WAN IP and the
first flow after the WAN comes up relearns `{extIP, nexthop, connected-route ARP}`
(`dir842-asic` `asic_up()` comment; the ARP window is `rtl819x_hwnat.c:430`). That is
sufficient — **do not add an `/etc/hotplug.d/net` reprogram hook**: the only safe way to
reprogram is the full ordered `restart` (which disarms `hwnat` first), and a hotplug
script firing during `ifup` is precisely the "gate before ifup" case that measured-broke
L2. If a hotplug hook is ever wanted, gate it on `flow_offloading_hw=1` and call
`/etc/init.d/rtl819x-hwnat-gate restart`, never the raw writes.

---

## 4. Acceptance numbers (iperf3) — the definition of "high performance" here

Bench rig: RX2L Pro as gateway; `wan` ← 1 Gbit/s upstream (PC-A, 2.5 GbE NIC recommended
so the DUT is always the bottleneck); `lan` ← PC-B. Both PCs `iperf3 -s`, DUT is the
client-of-both path. Single-core 999 MHz–1 GHz MIPS 24Kc, 128 MiB RAM, RGMII trunk at
1 Gbit/s full duplex. All figures are per single TCP stream unless stated.

Reference points I am calibrating against (all measured, same SoC family):

| config | NAT throughput | source |
|---|---|---|
| software only (no flowtable) | 766 / 703 Mbit/s (LAN→WAN / WAN→LAN) | base A snippet header |
| software flowtable on `eth0` | 850–880 Mbit/s | base A `05_enable_flow_offloading` |
| **hardware NAT** | **926–929 Mbit/s**, CPU 90–100 % idle | base A `05_enable_flow_offloading` |
| router-local traffic, CPU saturated | 194–199 Mbit/s | iptime `docs/performance.md` (explicitly *not* a routed figure) |

| # | test | command (run on PC-B) | PASS | EXCELLENT | FAIL ⇒ look here |
|---|---|---|---|---|---|
| A1 | NAT, WAN→LAN (download) | `iperf3 -c <wan-ip> -t 60 -p 5201` | ≥ 850 Mbit/s | ≥ 900 Mbit/s with `flow_offloading_hw=1` | < 700 ⇒ the `eth0` snippet is gone or `kmod-nft-offload` missing (frozen counters absent) |
| A2 | NAT, LAN→WAN (upload) | `iperf3 -c <wan-ip> -t 60 -R` | ≥ 850 | ≥ 900 | asymmetric failure ⇒ reply-direction NAPT row missing (`rtl819x_hwnat.c`: reply is identified by IP-mangle offset 16) |
| A3 | NAT, bidirectional | `iperf3 -c <wan-ip> -t 60 --bidir` | ≥ 800 each way | ≥ 900 each way | trunk is 1 Gbit/s **per direction** — ≥ 900 both ways is legitimate, do not chase 1000 |
| A4 | NAT UDP loss floor | `iperf3 -u -c <wan-ip> -b 900M -t 30` | loss ≤ 0.1 % | 0.00 % | loss with CPU busy ⇒ CPU path, not ASIC |
| A5 | **proof it is hardware** | A1 while running `conntrack -L -p tcp --dport 5201 \| grep -o 'bytes=[0-9]*'` twice, 10 s apart | counters **frozen** while throughput is full | also `hwnat=1` **and** `head -1 /proc/rtl819x_router`=1, `/proc/rtl865x_napt` shows ≥ 1 valid row | counters climbing ⇒ still software path (this is Phase-4 proof from our 4.14 tree) |
| B1 | LAN→LAN switching (2 clients behind the RB) | `iperf3 -c <other-lan-client> -t 60` from a LAN host | ≥ 940 Mbit/s | ≥ 941 (line rate) | if `eth0` rx+tx packet **deltas** are not ≈ 0 over the run, the frames traversed the CPU ⇒ switch VLAN/FDB wrong, offload irrelevant |
| B2 | 5-port wire speed (1 WAN + 3 LAN + CPU) | 3 parallel LAN→LAN streams (`-P 3` or 3 hosts) + 1 WAN↔LAN stream, 60 s | each LAN pair ≥ 900, WAN stream ≥ 850, **zero** iperf3 retrmits attributable to loss | aggregate ≥ 3.6 Gbit/s + NAT ≥ 850 concurrently | a pair collapsing to 100 Mbit/s ⇒ that jack negotiated 100BASE-T (see `phylink-notes.md`), not an offload bug; base A measured **85 Mbit/s** when one 100 Mbit device was in the flood set (`rtl819x_fdb` comment) |
| B3 | mixed NAT + switching soak | A1 + B2 together, 3600 s | no application failure; load ≤ 0.5; memory returns to pre-run level | iptime's own bar: 3 603 s soak, 713 pings/side, 119 × 1 MiB downloads, load ≤ 0.09 | wedge/FCS growth ⇒ `driver-port.md` §4 items 5 and 7 |
| C1 | flow capacity | `iperf3 -c … -P 64` plus 2 000 short connections | throughput stays ≥ 800 Mbit/s | — | the ASIC L4 table is `RTL865X_NAPT_ROWS = 1024` rows, flat-addressed; over-subscription must fall back to software, not drop. Collisions are visible as `collision` bits in `/proc/rtl865x_napt` and tunable via `napt_collision_prefill` |
| C2 | mode toggle stability | `uci set firewall.@defaults[0].flow_offloading_hw=1 && /etc/init.d/rpcd restart` | survives; gate reboots deliberately on a *change* | — | a hang here reproduces base A's 29.10 measurement; do **not** soften the `reboot` |

Report format for every run: `iperf3` sender + receiver lines, `mpstat`/`top` idle %,
`ethtool -S eth0` deltas, `swconfig dev switch0 port N show` counters,
`/proc/rtl865x_napt` row count. A number without the CPU-idle and counter deltas next to
it does not distinguish "fast" from "fast because the CPU is doing twice the work".

---

## 5. First-boot enable sequence (the short version to paste into the checklist)

```sh
# 0. datapath sanity before touching offload at all
ip -br link; cat /sys/class/net/eth0/carrier; swconfig dev switch0 show | head -40
cat /proc/rtl819x_spa                      # spa counters moving?
# 1. software offload only
uci set firewall.@defaults[0].flow_offloading=1
uci set firewall.@defaults[0].flow_offloading_hw=0
uci commit firewall; /etc/init.d/firewall reload
nft list table ip filter | grep -A3 flowtable      # ft must exist, WITHOUT `flags offload`
# 2. run A1/B1 ⇒ must hit ≥ 850 / ≥ 940
# 3. hardware
uci set firewall.@defaults[0].flow_offloading_hw=1; uci commit firewall
reboot            # the gate reload path reboots by design; runtime toggle is unsafe
nft list table ip filter | grep -A3 flowtable      # must now show `flags offload`
cat /sys/module/rtl819x/parameters/hwnat           # 1
head -1 /proc/rtl819x_router                        # 1
# 4. run A5 ⇒ frozen conntrack counters + a row in /proc/rtl865x_napt
```

If step 4's `nft` shows no `flowtable` at all, stop: that is `kmod-nft-offload`/
`kmod-nf-flow` missing, and every later measurement will mislead you into thinking the
hardware work "accomplished nothing".
