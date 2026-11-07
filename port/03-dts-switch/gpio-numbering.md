# GPIO numbering on RTL8197F: vendor `ugw_gpio` ids → device-tree cells

Everything here is derived from source we hold on disk. Line numbers are given
so the next reader can re-check instead of trusting me.

## 1. What stock actually uses

`/tmp/rx_dump/rootfs/x/etc_ro/gpio_conf` (the header of the file says
`#AX3V2.0 gpio配置脚本`, i.e. the vendor reuses the AX3V2 table for this
board). It drives the vendor misc class

```
cd /sys/devices/virtual/misc/ugw_gpio/gpio_base/
echo <slot> > gpio_index      # catalog slot, 0..63
cd <name>
echo <line> > gpio_id         # the hardware line
```

| consumer | `gpio_index` (slot) | `gpio_id` (line) | other fields |
|---|---|---|---|
| `sys_led` (green) | 0 | **54** | `freq 50`, `pattern 1`, `start 1` |
| `red_led` | 7 | **57** | `freq 50`, `pattern 2`, `start 1` |
| `reset_btn` | 32 | **33** | `freq 10`, `act_time 80`, `inact_time 80` |
| `wps_btn` | 33 | **33** | `freq 10`, `act_time 5`, `inact_time 30` |

`reset_btn` and `wps_btn` carry **the same `gpio_id`** and differ only in the
press-duration thresholds — one combined Reset/WPS button, two software
consumers. That matters: declaring two DT buttons on one line gives two
`input_dev`s racing on the same edge.

The `gpio_id` field is the hardware line; `gpio_index` is only the slot in the
vendor catalog (the module's own error string is `%s invalid index, use 0-63`,
see `strings` of `/tmp/rx_dump/rootfs/x/lib/modules/gpio.ko`, and the
attribute writers are `_gpio_index_store` / `_gpio_id_store`).

Independent confirmation that `gpio_id` is the *same* numbering as the BSP
pin index, from the RX23302 board (`/tmp/rebase/openwrt-rx23302/.../
rtl8197f_electra_rx23302.dts`, lines 43-46 and 69-82, both verified on their
hardware): their stock `gpio_led` structure reads
`wifi2.4=33, wifi5=100, status=57`, and the file then states

* status LED = line H1 = `&gpio1 25`, and H1 → 57,
* reset button = line 54 = "порт G пин 6" (port G pin 6) = `&gpio1 22`,
  proved by watching `0xB8003528` change bit 22 (`05C00001 → 05800001`),
* SMI = `&gpio1 24/23/26` for lines 56/55/58 = H0/G7/H2.

Their note also gives the arithmetic explicitly: *"Номер линии в gpio1 =
порт \* 8 + пин - 32"*.

## 2. The vendor line number

```
line = port * 8 + pin          with  A=0, B=1, C=2, D=3, E=4, F=5, G=6, H=7
```

| line | port/pin | direction of evidence |
|---|---|---|
| 54 | G6 | RX23302 hardware: pressing reset flips bit 22 of `0xB8003528` |
| 55 | G7 | SMI MDIO on both Tenda boards and on RX23302 (`&gpio1 23`) |
| 56 | H0 | SMI MDC on both Tenda boards and on RX23302 (`&gpio1 24`) |
| 57 | H1 | RX23302 status LED (`&gpio1 25`) |
| 58 | H2 | switch reset (`&gpio1 26`) |
| 33 | E1 | RX23302: "2.4G LED ... пин E1 не отдаётся без pinmux" and `wifi2.4=33` |

The register half of the story: bank `PABCD` (lines 0-31) lives at
`0x18003500` and bank `PEFGH` (lines 32-63) at `0x1800351C`, i.e. **line ≥ 32
means PEFGH and `bit = line - 32`**. That is not a convention I chose, it is
what the driver in base B does:

`/tmp/rebase-base/target/linux/realtek/files-6.6/drivers/gpio/gpio-rtl8197f.c`
(header comment lines 4-12 plus these helpers):

```c
static inline unsigned int rtl8197f_gpio_bit(unsigned int offset)
{ return offset & 0x1f; }
static inline u32 rtl8197f_gpio_reg_cnr(unsigned int offset)
{ return offset < 32 ? RTL8197F_GPIO_PABCD_CNR : RTL8197F_GPIO_PEFGH_CNR; }
```
with `PEFGH_CNR 0x1c, PEFGH_DIR 0x24, PEFGH_DAT 0x28, PEFGH_ISR 0x2c,
PEF_IMR 0x30, PGH_IMR 0x34` and `RTL8197F_GPIO_NGPIO 64`, and the binding
`ngpios` maximum of 64 in
`files-6.6/Documentation/devicetree/bindings/gpio/realtek,rtl8197f-gpio.yaml`.

`0x3500 + 0x28 = 0xB8003528` is exactly the address the RX23302 author
measured for line 54 → `PEFGH_DAT` bit 22 → `54 - 32 = 22`. Two independent
sources, one arithmetic.

## 3. Mapping table for THIS board

| function | vendor line | port/pin | 6.18 swconfig base (two 32-line chips) | 6.6 DSA base (one 64-line chip) |
|---|---|---|---|---|
| SMI MDC (switch) | 56 | H0 | `<&gpio1 24>` | `<&gpio0 56>` |
| SMI MDIO (switch) | 55 | G7 | `<&gpio1 23>` | `<&gpio0 55>` |
| switch reset | 58 | H2 | `<&gpio1 26>` | `<&gpio0 58>` |
| green LED | 54 | G6 | `<&gpio1 22>` | `<&gpio0 54>` |
| red LED | 57 | H1 | `<&gpio1 25>` | `<&gpio0 57>` |
| reset/WPS button | 33 | E1 | `<&gpio1 1>` | `<&gpio0 33>` |

Why the two bases differ: base A's `RTL8197F.dtsi` (lines 50-64) declares
`gpio0: gpio-controller@3500 { reg = <0x3500 0x1c>; }` and
`gpio1: gpio-controller@351C { reg = <0x351C 0x1c>; }` — the *same* hardware
block cut into its two halves, driven by
`files-6.18/arch/mips/realtek/gpio.c`, which uses bank-relative offsets
`0x00 CNR / 0x08 DIR / 0x0C DAT / 0x10 ISR / 0x14,0x18 IMR`
(lines 31-40, 78-96, 238-245: `dat = base + 0x0C`, `dirout = base + 0x08`).
`0x3500+0x1c = 0x351C` and `0x351C+0x1c = 0x3538`, so `gpio0` is PABCD and
`gpio1` is PEFGH and each chip has 32 cells (`if (gpio_pin >= chip->ngpio)
return -EINVAL;` at line 47, with the bgpio cell width of 32 bits). Base B
keeps one chip of 64 cells. Same pads, different cell numbers:
**base A cell = line − 32·(line ≥ 32), base B cell = line**.

Both drivers are selected by `compatible = "realtek,rtl8197f-gpio"`
(base A `gpio.c:208-211`, base B `gpio-rtl8197f.c` of_match table).

## 4. What still blocks enabling LEDs and keys

Not the arithmetic above — that is settled. Two real unknowns:

1. **The mux field for line 33 (port E1).** The SMI pair is released by an RMW
   of `0xB8000034` with value `0x03300000` and mask `0x0ff00000`, i.e. two
   4-bit fields at bits 20-23 and 24-27 (proved on *our* U-Boot:
   `uboot_gz.bin +0x6a68` and `+0x6c34` are `lui $v0,0x330 / and $v1,$v1,$v0 /
   or / sw`, with the mask loaded from `0x2c($sp)`). Which pad each of those
   two 4-bit fields belongs to is **not named in any source we hold** — the
   pairing with H0/G7 is only inferable from the fact that the value
   `0x03300000` releases exactly the SMI pair on three independent Tenda
   RTL8197F boards. E1's field is therefore unknown, and RX23302's own note
   says E1 stays on its peripheral function "without pinmux".
   Evidence that would settle it: read `0xB8000034` (and `0xB8000030/0x38`)
   before/after stock's `gpio_conf` runs, on a serial console —
   `md 0xb8000030` in U-Boot, or `devmem 0xB8000034` on stock. Any field that
   changes is E1's. Non-destructive: it is a read, and `gpio_conf` programs
   this anyway at boot.
2. **Polarity.** `gpio_id` in `gpio_conf` is written by an LED *pattern*
   engine (`freq`/`pattern`/`start`), which proves the line is an output, not
   that it is active-low. `ACTIVE_LOW` in the commented nodes is the sibling
   boards' convention, not our measurement. Evidence: `echo 0/1` to
   `/sys/class/gpio` after exporting, and watch the lamp — or a multimeter on
   the LED pad with the board powered.

One more hazard worth stating: a wrong LED line can hold the external switch
in reset. Line 58 (H2) is the switch's `~RESET,` and 54/57/58 are adjacent
H-bits; on the reference board this exact mistake produced "alive but
unreachable". Hence `status = "disabled"` in both DTS variants.
