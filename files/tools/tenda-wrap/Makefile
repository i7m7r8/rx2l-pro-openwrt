#
# Host-build package: tenda-wrap - 64-byte Tenda plaintext-image header writer.
#
# Copyright (C) 2026 rx2l-pro-openwrt contributors
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as published
# by the Free Software Foundation.
#
# WHAT THIS IS FOR
# ----------------
# `tenda-plaintext.bin` is the file shape the stock web UI / `/bin/decry_firm`
# path expects: our "cr6c" cvimg container preceded by a 64-byte Tenda header.
# This tool only prepends that header - it does not encrypt, sign, or touch the
# payload, and it is not a security boundary (see notes/tde01-verdict.md: the
# TDE01/RSA layer is checked by the *application* upgrade path only, never by
# the boot path, and a plaintext image installs anyway).
#
# Same packaging reason as tools-cvimg.Makefile: master's firmware-utils is a
# git-fed package without an in-tree src/, so this must be its own HostBuild.
#
# SOURCE PROVENANCE AND FIELD EVIDENCE
# ------------------------------------
# src/tenda-wrap.c is OUR source (identical to
# files/tools/firmware-utils/src/tenda-wrap.c; in this staging directory it is
# vendored at loader/src/tenda-wrap.c -> tools/tenda-wrap/src/tenda-wrap.c).
# Its field table was derived byte-exactly from two decrypted vendor images
# (RX2L Pro V16.03.51.17 and TX2L Pro V16.03.51.14) - see
# notes/tenda-plaintext-header.md. The two fields this recipe pins are:
#   +0x14  kernel reference word 0x808d45c0     (-k, per-product, non-derivable)
#   +0x18  CRC32 of the payload                 (computed here, not passed in)
# and -c cr6c (tenda-wrap.c:178, :186 - "reject the input unless its first 4
# bytes are STR") makes it refuse to wrap anything that does not literally
# begin with the four bytes `cr6c`, so a mis-ordered build step fails at build
# time instead of shipping a Tenda header around the wrong container.
#
# Compiles clean: gcc -Wall -O2 gives zero diagnostics for this source.
# Still no -Werror, same reason as cvimg (host toolchain drift must not be
# able to break image generation).
#
include $(TOPDIR)/rules.mk

PKG_NAME:=tenda-wrap
PKG_RELEASE:=1

include $(INCLUDE_DIR)/host-build.mk

define Host/Compile
	$(HOSTCC) $(HOST_CFLAGS) -Wall -o $(HOST_BUILD_DIR)/tenda-wrap src/tenda-wrap.c
endef

define Host/Install
	$(INSTALL_BIN) $(HOST_BUILD_DIR)/tenda-wrap $(STAGING_DIR_HOST)/bin/
endef

define Host/Clean
	rm -f $(STAGING_DIR_HOST)/bin/tenda-wrap
endef

$(eval $(call HostBuild))
