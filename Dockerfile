# Build container for the Tenda RX2L Pro (Realtek RTL8197F) OpenWrt port.
#
# The base tree is ggbruno/openwrt @ 8a0ccb9 (2020: kernel 4.14 / gcc 8.4), so
# it needs a Debian 11 (bullseye)-era host, python2 among the host tools, and a
# non-root build user -- OpenWrt refuses to build as root.
#
#   docker build -t owrt-rx2lpro .
#   docker run --rm -v "$PWD":/build -w /build owrt-rx2lpro ./build.sh
#
# If your host account is not uid 1000:
#   docker build --build-arg U=$(id -u) --build-arg G=$(id -g) -t owrt-rx2lpro .
FROM debian:bullseye

# Debian 11 is past EOL, so deb.debian.org no longer serves the point versions
# the debian:bullseye image's baked apt lists name -- every fetch 404s and the
# install dies. Redirect to the archive and stop honouring the expired Release
# validity window. Verified: each .deb that 404s on the mirror resolves 200
# under archive.debian.org/debian-security.
RUN set -eux; \
    sed -i -e 's|https\?://deb\.debian\.org/debian-security|http://archive.debian.org/debian-security|' \
           -e 's|https\?://deb\.debian\.org/debian|http://archive.debian.org/debian|' \
           /etc/apt/sources.list; \
    printf 'Acquire::Check-Valid-Until "false";\n' > /etc/apt/apt.conf.d/99archive; \
    apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      build-essential clang flex bison g++ gawk gcc-multilib g++-multilib \
      gettext git libncurses5-dev libssl-dev rsync unzip zlib1g-dev file \
      wget curl subversion swig time xsltproc libelf-dev bc python3 python2 \
      python-is-python2 ca-certificates ccache quilt \
      device-tree-compiler libpython2.7 && \
    rm -rf /var/lib/apt/lists/*

ARG U=1000
ARG G=1000
RUN groupadd -g "$G" builder 2>/dev/null || true; \
    useradd -u "$U" -g "$G" -m -s /bin/bash builder
USER builder
ENV HOME=/home/builder FORCE_UNSAFE_CONFIGURE=1
WORKDIR /build
