#
# image/rtl8197f.mk -- device profiles for the RTL8197F subtarget.
#
# Derived from port/02-image-container/image-Makefile.fragment, which carries the
# per-line evidence (boot-contract addresses from the mtd1 disassembly, container
# shape from three byte-exact reproductions, and the 6.18 master facts). This
# file is the same content with the device name settled, so it can be dropped in
# as target/linux/rtl819x/image/rtl8197f.mk and picked up by the platform's own
# image/Makefile (`include $(SUBTARGET).mk`).
#
# NAMING, and why it is not cosmetic: this subtree names the board
# `tenda_rx2lpro` everywhere else -- seed.config, the target's platform.sh,
# port/07-usersland, port/09-integration. OpenWrt builds the Kconfig symbol as
# DEVICE_$(subst -,_,$(DEVICE_NAME)) (include/image.mk), so a device declared as
# `tenda_rx2l-pro` yields CONFIG_TARGET_rtl819x_rtl8197f_DEVICE_tenda_rx2l_pro
# and the seed's ..._DEVICE_tenda_rx2lpro is dropped by `make defconfig` with no
# error -- the same silent-drop failure the build script's defconfig gate exists
# to catch. Hence: no hyphen.
#
# The platform overlay also ships an rtl8197f.mk (Device/electra_rx23302, another
# vendor's RTL8197FH board). This file replaces it rather than adding to it: the
# board we can measure is ours, and an untested profile in the same subtarget is
# a choice the build should not offer. If that board is ever wanted back, its
# four CVIMG_* values from the overlay are the only thing to restore.
#
# These four are NOT in master's DEFAULT_DEVICE_VARS, so they must be declared
# or every profile silently gets the empty value (verified 2026-11-07 against
# include/image.mk's DEFAULT_DEVICE_VARS list; IMAGE_SIZE / BLOCKSIZE / CMDLINE /
# KERNEL_SIZE *are* in that list and no longer need declaring -- drop the
# DEVICE_VARS += IMAGE_SIZE the 4.14 Makefile carries).
DEVICE_VARS += CVIMG_KERNEL_BURN_ADDR CVIMG_LOADER_START_ADDR CVIMG_TENDA_KREF
DEVICE_VARS += RX2LPRO_KERNEL_BUDGET

# ---------------------------------------------------------------------------
# 3. Loader invocation (unchanged values, new reason)
# ---------------------------------------------------------------------------
# LZMA_TEXT_START is the address the *loader* links at, and it must equal
# CVIMG_LOADER_START_ADDR below, because that header field is simultaneously the
# copy destination and the jump target [F §1,§3]. LOADADDR is where the loader
# decompresses the kernel to: 0x80000000, the base of the memory node in
# RX2LPRO.dts (`reg = <0x0 0x8000000>`) -- if the kernel/DTS track moves the
# link address, both this line and CVIMG_LOADER_START_ADDR's budget change.
# SUBTARGET is passed explicitly (NOT inherited) -- see loader/LOADER.md: the
# in-tree loader sources choose -march=mips32r2 vs lx4380/-D__RLX__ on the
# literal string "rtl8197f".
define Build/rx2lpro-loader
	rm -rf $@.src
	$(MAKE) -C lzma-loader \
		PKG_BUILD_DIR="$@.src" \
		TARGET_DIR="$(dir $@)" LOADER_NAME="$(notdir $@)" \
		BOARD="$(DEVICE_NAME)" \
		SUBTARGET=rtl8197f \
		LZMA_TEXT_START=$(CVIMG_LOADER_START_ADDR) LOADADDR=0x80000000 \
		LOADER_DATA="$@" KERNEL_CMDLINE="$(CMDLINE)" \
		compile loader.bin
	mv "$@.bin" "$@"
	rm -rf $@.src
endef

# ---------------------------------------------------------------------------
# 4. cvimg container writer
# ---------------------------------------------------------------------------
define Build/cvimg
	$(STAGING_DIR_HOST)/bin/cvimg \
		-i $@ -o $@.new \
		$(1) && mv $@.new $@ || rm -f $@
endef

# -s cr6c  : the signature our stage-1 accepts is `cr6c` or `cs6c` only [F §2].
#            `cr6c` = DATA_FIRMWARE + CPU_NEW in cvimg.c's own table, i.e.
#            "kernel AND rootfs in this one container" -- which is exactly the
#            stock shape [c] and exactly what the vendor's own AUTOBURN parser
#            labels "kernel-fs" [F §4]. Use -s (explicit) rather than
#            -t fw -c new: same bytes, but it cannot drift if cvimg's table
#            changes. `-c new -t kernel` would emit `cs6c` -- also bootable,
#            but it lies about the rootfs and is not the stock shape.
#            NEVER `cr6b`: that is the D-Link DIR-842 R1 signature and our
#            loader refuses it ("No sys signature").
# -e       : header +0x04 = jump target = loader link address, written BE.
#            0x81000000 is correct and safe [F §6]: the loader is header-driven,
#            nothing in mtd1 forces the vendor's 0x80d00000 (that constant
#            appears nowhere in mtd1 or in U-Boot proper). 0x81000000 also
#            raises our decompressed-kernel budget from 13 MiB to 16 MiB.
#            Constraint: it must stay an 0x8xxxxxxx KSEG0 address, because
#            stage-1 forms the copy destination as `start_addr | 0x20000000`.
# -b 0x40000 : header +0x08. Unused by the boot path [F §3], but written as the
#            real flash offset anyway: it is what stock carries [c], it is what
#            the vendor updater/AUTOBURN path reads, and it is the offset the
#            r6cr/jffs2 maths inside cvimg.c keys off (`-b` + file size).
#            0x40000 = mtd3 KernelFS base (measured: mtd1 0..0x30000,
#            mtd2 0x30000..0x40000, mtd3 0x40000..0xfc0000, erasesize 0x10000).
#
# What cvimg writes for `len` (+0x0c, BE) is `payload rounded up to 2 + 2`
# (cvimg.c:554/559). That single field drives BOTH the checksum length and the
# copy length, so the squashfs must start at exactly 16+len -- which is what
# "append-rootfs immediately after cvimg-kernel-cr6c" gives us, and is what the
# stock container does (mtd3: len 0x3c0802, superblock at 0x3c0812) [c].
# The 2 checksum bytes are cvimg's `htons(~sum16+1)` stored BE at 16+payload,
# which makes the loader's "sum over len bytes from +0x10 == 0" identity close.
#
# Do NOT use cvimg's -T ("Tenda bootloader") mode: it writes `len` 16 bytes
# shorter and moves the checksum [c]; the stock RX2L Pro container is standard
# format, verified.
define Build/cvimg-kernel-cr6c
	$(call Build/cvimg, \
		-e $(CVIMG_LOADER_START_ADDR) \
		-b $(CVIMG_KERNEL_BURN_ADDR) \
		-s cr6c \
		$(1))
endef

# ---------------------------------------------------------------------------
# 5. The 64-byte Tenda plaintext header (tenda-wrap)
# ---------------------------------------------------------------------------
# Emit shape of the vendor's own /bin/decry_firm output [t]. Fields, all BE:
#   +0x00 27 05 19 56      constant (uImage magic)          -- tool constant
#   +0x0c payload length    = file size - 0x40               -- computed
#   +0x10 0x80000000       constant                          -- tool constant
#   +0x14 kernel reference  0x808d45c0 for RX2L Pro         -- CVIMG_TENDA_KREF
#           Not content-derived (RX vs TX2L differ by exactly 0x10, and it does
#           not track the payload), so nothing can validate it; we ship the
#           value observed in this product's own image. It is NOT a secret and
#           NOT per-unit.
#   +0x18 CRC32(payload)                                   -- computed
#           Post-decryption integrity field, computable by anyone; it guards
#           nothing. Our tool must fill it or the file is malformed.
#   +0x1c 05 05 02 03 02 00 00 00 / +0x24 0x00031053        constants
#   +0x28 total length      = payload + 0x40                 -- computed
#   +0x2c 20 bytes of zeros
# +0x04/+0x08 are per-build and non-derivable; tenda-wrap keeps the stock
# observed words unless -b is given. Nothing here is a security boundary: the
# only gate is the RSA-4096/SHA-512 signature in the TDE01 wrapper, which we
# cannot and do not reproduce (notes/tde01-verdict.md) -- so
# tenda-plaintext.bin is for the `tendaupgrade`/staged-file install path, not
# for the stock webui Local Upgrade.
#
# The checker in tools/check-cr6c.py and tests/test_container.py both re-derive
# +0x0c/+0x18/+0x28, so a wrong kref or a stale crc32 fails CI, not the router.
define Build/tenda-wrap
	$(STAGING_DIR_HOST)/bin/tenda-wrap -i $@ -o $@.new -c cr6c \
		-k $(CVIMG_TENDA_KREF) && mv $@.new $@ || rm -f $@.new
endef

# ---------------------------------------------------------------------------
# 6. Hard kernel-size gate (the "fail loudly" part)
# ---------------------------------------------------------------------------
# See the budget analysis in REPORT.md. The stage-1 loader copies the payload
# to start_addr and jumps there; the loader decompresses the kernel downward
# from LOADADDR=0x80000000 while it is still executing from 0x81000000, and the
# compressed stream sits above it in the same payload. So the decompressed
# kernel+DTB must stay under `CVIMG_LOADER_START_ADDR - LOADADDR` = 16 MiB.
# The in-tree loader has NO bound check of any kind (loader.c just calls
# LzmaDecode into LOADADDR), so an oversized kernel is a silent, unbootable
# image -- OpenWrt's own Build/check-size is deliberately non-fatal (it prints
# "WARNING: Image file ... is too big" and `rm -f` the image, verified in
# include/image-commands.mk:316-323), which is too late and too quiet here.
# The gate therefore runs INSIDE the kernel pipeline, on the uncompressed
# kernel+DTB, and exits non-zero:
define Build/rx2lpro-kernel-gate
	python3 $(TOPDIR)/scripts/rx2lpro-image-gates.py kernel \
		--image "$@" \
		--loadaddr 0x80000000 \
		--loader-start $(CVIMG_LOADER_START_ADDR) \
		--reserve 64k \
		--label $(DEVICE_NAME)
endef

# ... and a second, also-fatal gate on the finished combined image, so an
# image that no longer fits mtd3 can never be produced. 15872k = 0xf80000
# (the whole mtd3 KernelFS region). padjffs2 rounds up to a 64k erase sector,
# so this is evaluated before pad-rootfs AND again after it.
define Build/rx2lpro-firmware-budget
	python3 $(TOPDIR)/scripts/rx2lpro-image-gates.py budget \
		--image "$@" \
		--limit $(IMAGE_SIZE) \
		--erase-size $(BLOCKSIZE) \
		--pad \
		--label $(DEVICE_NAME)
endef

# ---------------------------------------------------------------------------
# 7. What we deliberately do NOT copy from the two reference bases
# ---------------------------------------------------------------------------
# * rx23302's Build/rootfs-chksum (scripts/rtl819x-rootfs-chksum.py). That
#   exists because the *Electra* vendor loader reads a rootfs length out of
#   squashfs superblock +0x08, adds 642, and demands a BE-u16 zero-sum trailer,
#   else "rootfs checksum error at ...". Our stage-1 never gets there: the
#   rootfs-signature branch (0x800109e4) is only reached when the checksum loop
#   returns exactly 2 [F §2], and that return value is the read-loop counter
#   (~len/256; ~15365 on the stock container) -- i.e. it would need a ~512-byte
#   kernel. Shipping that trailer would mean overwriting squashfs's mkfs_time
#   field for nothing. Same verdict for toby's
#   rtl8197f-rootfs-checksum.py. IF a future dump shows a Tenda stage-1 variant
#   that checks it, the gate to add is one line here, not a re-plan.
# * The vendor's squashfs magic mangling (`hsqs` -> `nice`) [c]: a stock-kernel
#   patch, not a container requirement. Our kernel and our mtdsplit both use
#   normal `hsqs`; mangling ours would break the rootfs split.
# * cvimg-pad-rootfs (`cvimg -j`, the trailing `r6cr` + jffs2 cleanmarker
#   container) from the 4.14 recipe. Measured: the device's own mtd3 dump
#   contains no `r6cr` anywhere and zero `deadc0de` markers (checked across all
#   0xf80000 bytes), and the decrypted vendor image is one cr6c container plus
#   squashfs [t]. The marker sits outside `len`, so the loader ignores it, and
#   mount_root erases that erase block on first overlay format anyway. Plain
#   `pad-rootfs` is both what stock looks like and what OpenWrt expects.
#   (BLOCKSIZE := 64k is load-bearing here: padjffs2 otherwise tries
#   4/8/16/64/128/256k and can waste up to 192 KiB -- same conclusion the
#   toby base reached for its Tenda boards.)
# * append-fake-rootfs. The 4.14 recipe already dropped it for this device and
#   the reason still holds: with no FAKE block between kernel and rootfs,
#   mtdsplit_cvimg's direct check at 16+len lands exactly on the superblock and
#   the 0xa0000-step scan (whose first hit can be a false positive inside the
#   kernel blob) never runs.

# ---------------------------------------------------------------------------
# 8. The device block
# ---------------------------------------------------------------------------
# Naming: master dropped DEVICE_TITLE/PROFILES; the profile name comes from
# `define Device/<name>` above, and the hyphen-free spelling is load-bearing
# (see the header of this file). DEVICE_VENDOR/DEVICE_MODEL only set the
# metadata strings and the image prefix.
# KERNEL wraps at the KERNEL stage (primary-base style) rather than at the
# IMAGE stage (how the 4.14 recipe did it) on purpose: master defaults
# KERNEL_INITRAMFS to $(KERNEL) (include/image.mk:457), so
# ...-initramfs-kernel.bin comes out as a loader-bootable cr6c container too.
# That is the one artifact we can hand the realtek command monitor
# (`XMOD` to RAM + `J: <addr>`) or the disaster-TFTP path and get a
# no-flash-write boot test.
define Device/tenda_rx2lpro
  SOC := rtl8197f
  DEVICE_VENDOR := Tenda
  DEVICE_MODEL := RX2L Pro
  DEVICE_DTS := rtl8197f_tenda_rx2lpro
  DEVICE_DTS_DIR := ../dts
  SUPPORTED_DEVICES := rx2l-pro
  BLOCKSIZE := 64k
  IMAGE_SIZE := 15872k
  # 115200, read off this board's own /proc/cmdline (the DIR-842 reference is
  # 38400). lpj= is deliberately absent -- the vendor sets it, we do not copy
  # a value we have not measured on 6.18.
  CMDLINE := console=ttyS0,115200 rootfstype=squashfs,jffs2
  KERNEL := \
    kernel-bin | \
    append-dtb | \
    rx2lpro-kernel-gate | \
    lzma | \
    rx2lpro-loader | \
    cvimg-kernel-cr6c
  KERNEL_INITRAMFS = $$(KERNEL)
  # header +0x04 == the loader's link address (see §3/§4). One variable is the
  # single source of truth for both, so they cannot disagree -- which is the
  # exact way this image either boots or prints nothing at all.
  CVIMG_LOADER_START_ADDR := 0x81000000
  CVIMG_KERNEL_BURN_ADDR := 0x40000
  CVIMG_TENDA_KREF := 0x808d45c0
  RX2LPRO_KERNEL_BUDGET := 0x1000000
  IMAGES := factory-cr6c.bin tenda-plaintext.bin sysupgrade.bin
  # 1. The bare container + rootfs: what the loader's AUTOBURN/TFTP path wants,
  #    what a direct `mtd write ... KernelFS` wants, and the shape stock mtd3
  #    has. Rootfs appended immediately: no gap, no FAKE block, no trailer.
  IMAGE/factory-cr6c.bin := \
	append-kernel | \
	append-rootfs | \
	rx2lpro-firmware-budget | \
	pad-rootfs | \
	rx2lpro-firmware-budget
  # 2. The same bytes behind the 64-byte Tenda plaintext header -- the shape
  #    /bin/decry_firm emits and /var/image expects (install path 3).
  IMAGE/tenda-plaintext.bin := \
	append-kernel | \
	append-rootfs | \
	rx2lpro-firmware-budget | \
	pad-rootfs | \
	tenda-wrap
  # 3. sysupgrade: RAW, not sysupgrade-tar. PART_NAME=firmware +
  #    default_do_upgrade pipes `get_image` straight into
  #    `mtd write - firmware` (package/base-files/files/lib/upgrade/common.sh
  #    :309-318, and get_image() there only cat/zcats -- it does NOT unpack a
  #    tar), and validate_firmware_image() calls platform_check_image() on the
  #    file as given. So a tar would (a) be written verbatim into `firmware`
  #    and (b) fail our own "magic must be 63723663 / cr6c" gate. The 4.14
  #    recipe's `sysupgrade-tar` is a latent bug; this is the fix. Same bytes
  #    as factory + metadata, which is also what makes an interrupted upgrade
  #    recoverable by re-running the same file.
  IMAGE/sysupgrade.bin := \
	append-kernel | \
	append-rootfs | \
	rx2lpro-firmware-budget | \
	pad-rootfs | \
	rx2lpro-firmware-budget | \
	append-metadata | \
	check-size
  # 2.4 GHz is the on-SoC WMAC (the overlay's rtl8192fe HAL, track 05); 5 GHz is
  # RTL8832BR and has no 6.x driver anywhere (track 06). Neither belongs in an
  # image-block decision, but both are budget-relevant: see REPORT.md.
  #
  # kmod-rtl8192cd is deliberately NOT listed yet. It was in the track-02 draft,
  # but nothing on this base defines that package: the only definition that ever
  # existed was the 4.14 one (modules.mk, FILES=$(LINUX_DIR)/.../rtl8192cd.ko),
  # now staged at port/05-wifi-2g/vendor-4.14/ because a KernelPackage whose
  # FILES path no longer exists installs an empty module and fails the image
  # build at the very end, hours in. An absent radio is a smaller failure than a
  # defconfig symbol that cannot build; track 05 adds this back together with the
  # package definition that makes it true.
  DEVICE_PACKAGES := kmod-leds-gpio kmod-gpio-button-hotplug \
	ppp ppp-mod-pppoe kmod-nf-flow kmod-ipt-offload iperf3 tcpdump-mini \
	swconfig
endef
TARGET_DEVICES += tenda_rx2lpro
