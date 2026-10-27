#!/bin/sh
# Build libdmaheap_shim.so for Android arm64 with stock clang+lld (no NDK).
# Usage (from this dir): docker run --rm -v "$PWD":/src -w /src alpine:3.20 sh build.sh
set -e
command -v clang >/dev/null || apk add --no-cache clang lld >/dev/null
clang --target=aarch64-linux-android29 -O2 -fPIC -ffreestanding -fno-stack-protector \
  -nostdlib -shared -fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,-z,noexecstack \
  -Wl,--hash-style=both -Wl,-soname,libdmaheap_shim.so \
  -o libdmaheap_shim.so dmaheap_shim.c
llvm-readelf --dyn-syms libdmaheap_shim.so 2>/dev/null | grep -E "ioctl|__errno" || true
