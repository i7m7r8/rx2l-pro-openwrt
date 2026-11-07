#
# Host-build package: cvimg - Realtek cvimg container writer.
#
# Copyright (C) 2026 rx2l-pro-openwrt contributors
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as published
# by the Free Software Foundation.
#
# WHY THIS PACKAGE EXISTS AT ALL
# ------------------------------
# target/linux/<target>/image/Makefile calls
# `$(STAGING_DIR_HOST)/bin/cvimg` to wrap the LZMA kernel(+rootfs) payload in
# the 16-byte Realtek IMG_HEADER_T ("cr6c") container our bootcode refuses to
# boot without. Upstream used to ship cvimg in tools/firmware-utils, but on
# current master firmware-utils is a git-fed package (PKG_SOURCE_PROTO:=git on
# $(PROJECT_GIT)/project/firmware-utils.git) whose upstream repo has no
# cvimg.c and no `src/` directory in tree. Dropping our file into
# tools/firmware-utils/src/ would be silently ignored: the build only ever
# compiles what the tarball/git checkout unpacks.
#
# So the tool needs its own standalone HostBuild package - exactly the move
# the primary 6.18 base made for its own variant of this container. Its
# Makefile is the template; the only things changed here are the source, the
# extra flags we pass (see below) and the warning suppression.
#
# SOURCE PROVENANCE
# -----------------
# src/cvimg.c is OUR source, byte-identical to the one already in this repo at
# files/tools/firmware-utils/src/cvimg.c (it was written against the vendor's
# own container, not against any OpenWrt tree, so it carries no 4.14 legacy).
# In this staging directory it is vendored at loader/src/cvimg.c; the integrate
# mapping is:
#
#     loader/tools-cvimg.Makefile   ->   tools/cvimg/Makefile
#     loader/src/cvimg.c            ->   tools/cvimg/src/cvimg.c
#
# If the orchestrator prefers a single copy of the truth, copy from files/ at
# integrate time instead - but then files/tools/firmware-utils/ must go,
# otherwise two divergent cvimg.c files exist in the tree.
#
# WHAT THE RECIPE ACTUALLY RELIES ON IN THE C CODE
# ------------------------------------------------
#  * hdr.len (BE u32 at +0x0c) = align(payload,2) + 2   (cvimg.c:554, :559)
#  * the 2-byte checksum = htons(~sum16 + 1) stored BE at 16 + align(payload,2)
#    and computed over exactly align(payload,2) payload bytes
#    (cvimg.c:607-608) - which is why the loader's "additive BE-u16 sum over
#    `len` bytes starting at container+0x10 == 0" closes: that window is
#    payload + checksum.
#  * the header itself (signature, start_addr, burn_addr, len) is OUTSIDE the
#    checksum window, so nothing here can detect a wrong start_addr. Our image
#    recipe compensates with a build gate, not with this tool.
#  * -T (Tenda bootloader mode) writes `len` 16 bytes differently and is NOT
#    used by our recipe; `-j` (append r6cr/jffs2 end marker) is NOT used either.
#    See ../image-Makefile.fragment section 7 for why.
#
# WARNINGS
# --------
# gcc -Wall reports 4 diagnostics in this source: two `%lu` vs `int` format
# mismatches in a printf, one unused `fake_rootfs_start` (left over from the
# -j path), and one `chksum may be used uninitialized` that is a false
# positive - in the code path we use, chksum is assigned at cvimg.c:607 before
# the print at :628; the warning comes from the -T branch that never sets it.
# All four are print-path only and cannot change output bytes, so we compile
# with -Wall but deliberately WITHOUT -Werror: a host-glibc/gcc version bump
# must not be able to break image generation, and the tool's correctness is
# enforced by scripts/rx2lpro-image-gates.py on its output instead.
#
include $(TOPDIR)/rules.mk

PKG_NAME:=cvimg
PKG_RELEASE:=1

include $(INCLUDE_DIR)/host-build.mk

# `include/host-build.mk`'s Host/Prepare/Default copies ./src/* into
# $(HOST_BUILD_DIR), but Host/Compile/Install recipes run with the cwd of this
# package directory, so `src/cvimg.c` below is this file's own source tree.
define Host/Compile
	$(HOSTCC) $(HOST_CFLAGS) -Wall -o $(HOST_BUILD_DIR)/cvimg src/cvimg.c
endef

define Host/Install
	$(INSTALL_BIN) $(HOST_BUILD_DIR)/cvimg $(STAGING_DIR_HOST)/bin/
endef

define Host/Clean
	rm -f $(STAGING_DIR_HOST)/bin/cvimg
endef

$(eval $(call HostBuild))
