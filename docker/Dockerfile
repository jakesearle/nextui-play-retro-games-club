# The NextUI toolchains ship gcc, ninja and the target sysroot, but no Meson.
# Adding it in a cached layer keeps builds fast.
#
# Pinned by digest because one image now builds for every platform, so its ABI
# baseline has to stay fixed. Same digest Apostrophe pins for `make universal`.

ARG TOOLCHAIN=ghcr.io/loveretro/tg5040-toolchain@sha256:f131c6af64029a8723d0ce8d3c2682642f5f091b04714f6beedda9bec18477ab
FROM ${TOOLCHAIN}

RUN apt-get update \
 && apt-get install -y --no-install-recommends meson \
 && rm -rf /var/lib/apt/lists/*
