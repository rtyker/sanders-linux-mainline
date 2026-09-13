FROM debian:13

# Deps para 02-build-kernel.sh (kernel arm64 mainline):
# - gcc-aarch64-linux-gnu / binutils: cross toolchain (ARM64_CC)
# - device-tree-compiler: DTC do sanders
# - bc flex bison: kconfig/lexers
# - ccache: cache de compilação (CCACHE_DIR montado do host)
# - libssl-dev libelf-dev: hostprogs do kernel
# - make gcc libc6-dev perl: build base
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        gcc-aarch64-linux-gnu \
        binutils-aarch64-linux-gnu \
        device-tree-compiler \
        bc flex bison \
        ccache \
        make gcc libc6-dev perl git \
        libssl-dev libelf-dev \
        mkbootimg e2fsprogs \
        xz-utils gzip rsync \
    && rm -rf /var/lib/apt/lists/*

ENV CCACHE_DIR=/proj/cache_ccache_aarch64
WORKDIR /proj/sanders-linux-mainline/scripts
