# loader/ — what the 6.18 base needs for the RX2L Pro boot payload

Deliverable 4 of `port/02-image-container/`. Two questions answered: which
files have to exist and be wired, and whether the loader sources we already
have can be reused unchanged. **Short answer: the sources are reusable but NOT
unchanged — two RTL8197F-specific fixes have to be carried, and one Makefile
condition is a live trap.**

Everything below was read out of the two reference trees on 2026-11-07, not
assumed:

| tree | path | role here |
|---|---|---|
| primary (6.18) | `/tmp/rebase/openwrt-rx23302/openwrt/target/linux/rtl819x/image/lzma-loader/` | the loader we adopt (same ISA, same base) |
| primary (6.18) | `/tmp/rebase/openwrt-rx23302/openwrt/tools/cvimg/` | the host-tool packaging pattern we copy |
| secondary (6.6) | `/tmp/rebase-base/target/linux/realtek/image/lzma-loader/src/{head.S,board.c}` | the two fixes to cherry-pick |
| secondary (6.6) | `/tmp/rebase-base/target/linux/realtek/rtkload/` | reference only, do NOT use (see §5) |
| our 4.14 port | base tree (ggbruno `Realtek` @8a0ccb9) `target/linux/realtek/image/lzma-loader/` | what we boot today; NOT vendored into `files/` |

--------------------------------------------------------------------------------
## 1. File list the new base needs
--------------------------------------------------------------------------------
`target/linux/<target>/image/lzma-loader/` — 16 files, all present and complete
in the primary base, so this is normally a no-op unless the rebase lands in a
target that has no rtl8197f subtarget at all:

    lzma-loader/Makefile              outer host-side driver (compile loader.bin)
    lzma-loader/src/Makefile          inner: flags, objects, link
    lzma-loader/src/head.S            startup: cp0 init, reloc, cache, stack
    lzma-loader/src/loader.c          lzma props read + LzmaDecode + jump
    lzma-loader/src/loader.lds        text at LZMA_TEXT_START, workspace symbol
    lzma-loader/src/loader2.lds       (unused by us: only for the loader.elf path)
    lzma-loader/src/lzma-data.lds     section for the embedded LZMA stream
    lzma-loader/src/board.c           UART putc + board_init
    lzma-loader/src/cache.c cache.h cacheops.h   R4K cache flush/invalidate
    lzma-loader/src/printf.c printf.h tiny printf (cmdline + diagnostics)
    lzma-loader/src/LzmaDecode.c LzmaDecode.h LzmaTypes.h   the decoder

Kernel side: nothing. The loader is host-built per device (`$(MAKE) -C
lzma-loader`), it is not a package, and it does not need a kernel config
symbol. The only kernel-side coupling is `LOADADDR` == the kernel's link
address == the `memory` node base (`0x80000000`, RX2LPRO.dts).

Host tools the image recipe calls (see `loader/tools-*.Makefile` here):

    tools/cvimg/       -> $(STAGING_DIR_HOST)/bin/cvimg      (container writer)
    tools/tenda-wrap/  -> $(STAGING_DIR_HOST)/bin/tenda-wrap (64-byte Tenda header)

Both sources are already in this repo (`files/tools/firmware-utils/src/`), but
on current master `tools/firmware-utils` is a git-fed package
(`PKG_SOURCE_PROTO:=git`, `$(PROJECT_GIT)/project/firmware-utils.git`,
`PKG_SOURCE_DATE:=2026-10-12`) whose upstream repo no longer carries
`cvimg.c` — verified against master today. So dropping our sources into
`tools/firmware-utils/src/` would simply be ignored. Standalone host-build
packages are mandatory; the primary base already made exactly that move and
its `tools/cvimg/Makefile` is the template (its own comment says why, in
Russian: "в актуальном OpenWrt firmware-utils подтягивается внешним
git-репозиторием, куда свой исходник не положишь").


## 2. Build wiring
--------------------------------------------------------------------------------
Already present in the primary base's `image/Makefile` and reproduced in
`../image-Makefile.fragment` as `Build/rx2lpro-loader`:

    $(MAKE) -C lzma-loader \
        PKG_BUILD_DIR="$@.src" \
        TARGET_DIR="$(dir $@)" LOADER_NAME="$(notdir $@)" \
        BOARD="$(DEVICE_NAME)" \
        SUBTARGET=rtl8197f \
        LZMA_TEXT_START=$(CVIMG_LOADER_START_ADDR) LOADADDR=0x80000000 \
        LOADER_DATA="$@" KERNEL_CMDLINE="$(CMDLINE)" \
        compile loader.bin
    mv "$@.bin" "$@"

Notes on the four values that matter:

* `LZMA_TEXT_START` **must** equal the cvimg `-e` argument. One Makefile
  variable (`CVIMG_LOADER_START_ADDR`) feeds both, so they cannot drift. The
  header field is simultaneously the copy destination (`start_addr|0x20000000`)
  and the jump target, per the mtd1 disassembly.
* `LOADER_DATA="$@"` embeds the LZMA stream into the loader image
  (`data.o` via `lzma-data.lds`, section `.data.lzma`, placed inside `.text`),
  so container payload = loader code + compressed kernel, and `workspace`
  lands right after it. That is why `start_addr - LOADADDR` is the whole
  budget (see the budget section of REPORT.md).
* `KERNEL_CMDLINE="$(CMDLINE)"` is baked into the loader (`CONFIG_KERNEL_CMDLINE`),
  so the console speed we can actually read comes from the recipe, not from
  the vendor's env. Keep `CMDLINE` here consistent with the DT `chosen`
  bootargs, or the two will fight.
* `BOARD="$(DEVICE_NAME)"` becomes `-DCONFIG_BOARD_TENDA_RX2L_PRO`
  (`src/Makefile: BOARD_DEF := $(shell echo $(BOARD) | tr a-z A-Z | tr - _)`).
  ⚠ master names devices `vendor_model` (`tenda_rx2l-pro`), the 4.14 tree used
  `DEV_PROFILE` (`RX2LPRO`). Any `#ifdef CONFIG_BOARD_...` you add must use the
  *new* spelling, and the two fixes in §3 are exactly such conditionals.


## 3. Reusable unchanged? NO — carry these two fixes
--------------------------------------------------------------------------------
### 3.1 SoC watchdog must be stopped inside the loader (blocks first boot)
The secondary base's `head.S` writes, before touching caches/BSS/UART:

    li   t0, 0xb800311c          # RTL8197F_WDTCNR
    lw   t1, 0(t0)
    li   t2, 0x00800000          # WDT_CLEAR (kick)
    or   t1, t1, t2
    sw   t1, 0(t0)
    sync
    li   t1, 0xa5000000          # WDT_STOP magic
    sw   t1, 0(t0)
    sync

with the comment "The OEM RTL8197F boot code leaves the SoC watchdog running
when it copies the cr6c payload to RAM. LZMA decompression of a Linux 6.6
kernel is long enough to trigger it", and the matching
`rtl8197f_watchdog_stop()` in `board.c` (`RTL8197F_WDTCNR 0xb800311cU`,
`RTL8197F_WDT_STOP 0xa5000000U`, `RTL8197F_WDT_CLEAR 0x00800000U`, "kick
before the exact disable magic", mirroring `rtl819x_wdt_stop()` in the vendor
SDK).

The primary base's loader has **no** watchdog write at all (grep for
`0xb800311c`/`0xb80031`/`watchdog` across
`rtl819x/image/lzma-loader/src/`: zero hits), and neither does our 4.14 tree's.
Our stage-1 clears `0xb8003000`, `0xb8003004` and `0xb8003114` immediately
before the jump (mtd1 disassembly, `0x8001092c..10940`) — that is *not* the
WDT control register at `0xb800311c`, so the risk transfers to us. Symptom if
we skip this fix: console prints "Decompressing kernel... " and the box
reboots — a loop that looks like a bad kernel, not like a watchdog.

Action: copy `rtl8197f_watchdog_stop()` + the early `head.S` sequence into the
loader we ship, gated on `CONFIG_BOARD_TENDA_RX2L_PRO` (and, if the primary
base's conditionals are kept, added to its board list). Cost: ~15 lines in two
files. Both reference sources agree on the register values, and they came from
the same vendor SDK family, so confidence is high; still worth confirming on
serial on first RAM-boot (the message ordering tells you).

### 3.2 UART TX must not block forever (observability, not correctness)
The primary base's `board_putc()` spins on `UART_LSR_THRE` with no timeout;
the secondary base added `uart_wait_tx_ready()` with a 1 000 000-iteration
budget so a wedged UART still lets the loader reach the kernel. For a board we
are bringing up blind, keeping the loader *printable* even when the console is
unhappy is worth the 8 lines. Note both trees already pick the TX register the
same way —

    if ((READREG(0xb8000000) & 0xfffff000) == 0x8197f000) tx = base+0x24; /* RTL8197F */
    else                                                   tx = base+0x00; /* -VG/-FH-VG */

— and our board's own U-Boot banner is `RTL8197F-VG`, which is consistent with
stage-1 itself poking the byte register at `0xb8147000` (mtd1, the "Booting..."
loop). So the auto-detect lands on `+0x00` for us, i.e. the loader prints on
the same console as stage-1. No change needed there; do not "simplify" it into
a constant.

### 3.3 The ISA trap in `src/Makefile` (silent, fatal)
Primary base `lzma-loader/src/Makefile`:

    ifeq ($(SUBTARGET),rtl8197f)
      CFLAGS += -march=mips32r2 -Wa,-march=mips32r2
    else
      CFLAGS += -march=lx4380 -Wa,-march=lx4380 -D__RLX__
    endif

Our SoC is MIPS 24Kc V8.5 (measured from `/proc/cpuinfo`), i.e. mips32r2, and
`__RLX__` also switches `UART_BASE` to the RTL8196-era `0xb8002000` with
`UART_SHIFT 24`. If the rebase lands this device in a subtarget whose name is
not literally `rtl8197f` — e.g. the whole thing moves under
`target/linux/realtek` with subtarget `rtl8197f` (fine) or a single
`rtl819x` subtarget (not fine) — the loader is built for the wrong CPU and
dies before a single byte of output. Therefore `image-Makefile.fragment`
passes **`SUBTARGET=rtl8197f` explicitly** instead of inheriting `$(SUBTARGET)`.
The secondary base dodges this entirely: its `src/Makefile:38` hardcodes
`-mabi=32 -march=mips32r2 -Wa,-mips32r2` for everyone. Either is acceptable;
what is not acceptable is depending on a directory name for the ISA.

Recommended belt: also keep a build-time assertion in the recipe, e.g.

    $(CROSS_COMPILE)objdump -f $@.src/loader | grep -q 'mips.*32.*r2'

or simply diff the loader's `.MIPS.abiflags` — cheap, and it converts a dead
serial port into a build error.


## 4. What we do NOT need from the secondary base's loader
--------------------------------------------------------------------------------
* **Payload self-relocation** (`head.S __reloc_copy`, and toby's
  `RTL8197F_LOADADDR` (0x80a00000) ≠ `RTL8197F_LZMA_TEXT_START` (0x80d00000)
  split, with `rtl8197f-loader-overlap-check.py` guarding the copy-overlap).
  That machinery exists because those boards' stock headers point at
  0x80a00000 and the decompressed kernel there overwrote a loader linked at
  the same address. We choose our own header, so `start_addr == link addr ==
  0x82000000` and the LZMA stream is read from *above* the destination the
  whole time: source ≥ 0x82000000, destination 0x80000000..0x82000000. No
  overlap is possible while the kernel stays under 32 MiB, which the gate
  enforces. One variable instead of two, no relocation, no overlap checker.
  (The value was 0x81000000 -- a 16 MiB window -- until the initramfs image
  measured 18,201,116 bytes and the gate refused it; see §6.)
* `ar71xx_regs.h`, `config.h`, `cp0regdef.h`, `cache_9xD.c`, `inflate.c` —
  APU/rtl838x leftovers of the secondary base.
* `rtkload/` as a *build input*: it is the vendor SDK loader dump (a
  prebuilt `cvimg` binary, `cvimg-32bit` 488 KB, `start_no_flash.S`,
  `script.no_flash`, `ld-emu.script`). Useful as evidence — its
  `LzmaDecode.c`/`misc.c` are what the strings in our stock container
  ("decompressing kernel:", "LZMA: Too big uncompressed stream",
  "start address: 0x%08x", "copying 0x%x bytes from file offset 0x%x to
  address 0x%08x") came from — but shipping it would mean adopting a loader
  whose output we cannot audit line by line. Keep it as documentation only.
* `-T` (cvimg "Tenda bootloader" checksum mode) and any `rtkload`-style
  secondary stub: our container carries our loader, and the stock RX2L Pro
  container is standard-format (verified).


## 5. First RAM-boot test this makes possible
--------------------------------------------------------------------------------
Because the recipe wraps `KERNEL_INITRAMFS` the same way as `KERNEL`
(`include/image.mk:457` defaults `KERNEL_INITRAMFS = $(KERNEL)`),
`openwrt-*-tenda_rx2l-pro-initramfs-kernel.bin` is a valid cr6c container
whose payload is a loader + an LZMA stream whose kernel has an embedded
initramfs. It needs no flash write:

1. serial + power-on with ESC (stage-1's ESC poll at 0x800109b8 aborts the boot
   into the RealTek command monitor), or the reset-strap disaster path;
2. `XMOD` the image into RAM at 0x82000000, then `J: 0x82000000`;
3. expected: `Jump to image start:0x82000000`, then the loader's own banner
   ("OpenWrt kernel loader for Realtek RTL819X"), `Decompressing kernel... done!`,
   `Starting kernel at 80000000...`.

That single boot proves the header contract, the 32 MiB budget, the ISA, the
watchdog fix and the console, all before anything is written to mtd3.

One thing that boot does *not* prove, and no dump can: that the stock stage-1
will hand control to a loader linked this high. §6 is the disassembly that
makes it a measurement rather than a hope.


## 6. start_addr is not constrained (disassembly of the stock stage-1)
--------------------------------------------------------------------------------
The decompiled loader is in `ghidra/stage1-jump-to-image.c` (provenance and
sha256 in its header). Two functions are the whole contract:

* `FUN_8001081c` -- reads 16 bytes from SPI flash, byte-swaps the BE words at
  +0x04/+0x08/+0x0c, then

      FUN_80009eb8(start | 0x20000000, flash + 0x50000010, len - 2);
      FUN_8000f8e4("Jump to image start:0x%x...", hdr[4]);
      pcVar4 = *(code **)(hdr + 4);
      (*pcVar4)();

  `start` is used verbatim: OR 0x20000000 to get the KSEG0 alias for the copy
  destination, and the raw 0x8xxxxxxx value as the jump target. Nothing is
  compared against it, and neither 0x81000000 nor the vendor's 0x80d00000
  occurs anywhere in the 229,584-byte image -- verified by grepping the whole
  decompiled dump, not just this function.
* `FUN_80010be4` -- the only rejection path before the jump: compare the 4-byte
  signature against `cs6c`/`cr6c` ("No sys signature at %X!"), then the
  additive BE-u16 zero-sum over `len` bytes from +0x10 ("check failed, ret:%d,
  sum:%x"). Its return value is the count of 256-byte blocks walked, which is
  what the rootfs-signature branch tests -- the vestigial path that would have
  needed a ~512-byte kernel.

Consequences, and why 0x82000000 is a safe constant for us:

1. Any `0x8xxxxxxx` address is honoured, so the window is ours to choose. The
   constraint is physical: `start | 0x20000000` must land in DRAM.
   0x82000000 -> 0xA2000000 -> physical 0x02000000 = 32 MiB into the 128 MiB
   this board has (`notes/firmware-audit.md` §10), so both the copy and the
   jump land in RAM.
2. The initramfs image decompresses to 18,201,116 bytes, which is why the
   16 MiB window had to go: the gate rejected it and the install stage stopped
   before producing any artifact (run 42453851939). 32 MiB leaves ~14.6 MiB of
   headroom over the current initramfs kernel.
3. The window is the only thing that changed. `LOADADDR` stays 0x80000000, the
   container layout is untouched, and the two values that must agree do agree
   by construction -- `CVIMG_LOADER_START_ADDR` is both the header word and
   `LZMA_TEXT_START` for the loader build (`image/rtl8197f.mk`).
