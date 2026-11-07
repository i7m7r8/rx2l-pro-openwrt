# Build container for the Tenda RX2L Pro (Realtek RTL8197F) OpenWrt port, on the
# 6.18 / latest-OpenWrt base.
#
# The base is plain upstream openwrt/openwrt @ 82e2e4f4 (kernel 6.18.41), so the
# host has to be a current Debian, not the Debian 11 image the 4.14 line needed.
# OpenWrt still refuses to build as root, so keep a non-root build user.
#
#   docker build -t owrt-rx2lpro .
#   docker run --rm -v "$PWD":/build -w /build owrt-rx2lpro ./build.sh
#
# If your host account is not uid 1000 (and in CI it often is not the image's
# uid), run as your own uid instead of rebuilding:
#   docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp/owrt-home \
#     -v "$PWD":/build:rw -w /build owrt-rx2lpro bash -c 'mkdir -p "$HOME" && ./build.sh'
#
# That -e HOME is not cosmetic: build.sh writes into the tree as your uid, and
# OpenWrt's host tools want a writable HOME (ccache, ~/.openwrt). Passing
# --user without HOME leaves HOME=/home/builder owned by 1000, and git then
# dies with "detected dubious ownership" -- exit 128 before a single compile.
# Proven on the 4.14 line's CI; carried over unchanged for that reason.
FROM debian:trixie

# Why trixie and not bookworm: the base's host prerequisites accept Python up to
# 3.14 and gate the distutils test behind `version_info < (3, 12)`
# (include/prereq-build.mk:210-213 at 82e2e4f4), so Debian 13's 3.13 is fine --
# and installing python3-distutils here would be wrong, it no longer exists as
# a package on 3.13. gcc/g++ >= 10 and GNU make >= 4.1 are required by the same
# file (:20-22, :34-52); trixie ships far past both.
#
# Dropped from the 4.14 image, with the reason each stopped mattering:
#   python2, python-is-python2, libpython2.7 -- the base's python checks look
#     for python3.8+ only (include/prereq-build.mk:189-208); nothing invokes
#     python2 any more.
#   subversion -- feeds.conf.new has two src-git entries and no src-svn, and
#     scripts/feeds only shells out to svn for src-svn/src-gitsvn (:146-150).
#   the archive.debian.org rewrite -- that was EOL damage: bullseye's point
#     releases stopped being served. trixie is current, so the normal mirror
#     answers, and keeping the archive redirect would be a slow lie.
# Added, with what pulls them:
#   rsync            hard requirement, include/prereq-build.mk:69 "Please install
#                      'rsync'" -- the 4.14 image had it, kept.
#   liblz4-dev       host mkfs.erofs is configured --enable-lz4 --with-uuid
#   uuid-dev           (tools/erofs-utils/Makefile:22-26). Only needed if a
#                      CONFIG_TARGET_ROOTFS_EROFS build is attempted; our seed
#                      is squashfs, this is the cheap insurance.
#   libzstd-dev      same story for compressed rootfs/kernel blobs.
#   libelf-dev       kernel objtool/BTF and the qosify eBPF path
#                      (package/network/config/qosify/Makefile:22
#                      PKG_BUILD_DEPENDS:=bpf-headers).
#   device-tree-compiler  our board is DTB-appended (append-dtb in the base's
#                      image pipeline); the kernel can build its own dtc, but a
#                      broken .dtb is debugged with `dtc -I dtb -O dts`, and
#                      that is the tool.
#   ca-certificates  every download in the build is https now.
# No cmake/meson/ninja here on purpose: the base builds its own host copies
# (tools/Makefile:38,56,62 `tools-y += cmake/meson/ninja`), and a system one
# that disagrees with the built one is how host-tool builds get weird.
# Likewise no f2fs-tools: this base has no f2fs rootfs type at all
# (include/image.mk:119 lists the fs-types it can build, squashfs/erofs/jffs2/
# ubifs/ext4 -- no f2fs), so a host mkfs.f2fs would be dead weight.
RUN set -eux; \
    apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      build-essential clang flex bison g++ gawk gcc-multilib g++-multilib \
      gettext git libncurses-dev libssl-dev rsync unzip zlib1g-dev file \
      wget curl swig time xsltproc libelf-dev bc \
      python3 python3-setuptools python3-pip \
      ca-certificates ccache quilt device-tree-compiler \
      liblz4-dev libzstd-dev uuid-dev perl && \
    rm -rf /var/lib/apt/lists/*

# The Perl module checks in include/prereq-build.mk:80-101 (Data::Dumper,
# FindBin, File::Copy, File::Compare, Thread::Queue, IPC::Cmd) are all satisfied
# by Debian's base `perl`, which is why there is no list of CPAN packages here.
# If a future base starts requiring a named module, the error is explicit
# ("Please install the Perl X module"), not a mystery.

ARG U=1000
ARG G=1000
RUN groupadd -g "$G" builder 2>/dev/null || true; \
    useradd -u "$U" -g "$G" -m -s /bin/bash builder
USER builder
ENV HOME=/home/builder FORCE_UNSAFE_CONFIGURE=1
# Case-sensitive fs and umask 022 are checked at build time
# (include/prereq-build.mk:24-31); /build must be a real, case-sensitive mount,
# which a bind-mount of a Linux host checkout is.
WORKDIR /build
