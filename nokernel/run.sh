#!/bin/sh
# Start redroid with the nokernel workarounds (see README.md).
#
#   ./prepare.sh 14 && ./run.sh 14 [name] [host-adb-port]
#
# Binder is still required and must be set up on the host first.
set -eu
v=$1; name=${2:-redroid$v}; port=${3:-5555}
img=${REDROID_IMAGE:-redroid/redroid:$v.0.0_64only-latest}
here=$(cd "$(dirname "$0")" && pwd)
out=$here/out/$v
[ -d "$out/com.android.adbd" ] || { echo "run ./prepare.sh $v first"; exit 1; }
set -- --name "$name" --privileged -p "$port:5555" \
  -e NK_DMAHEAP=1 \
  -v "$here:/.nk:ro" \
  -v "$out/com.android.media.swcodec:/system/apex/com.android.media.swcodec:ro" \
  -v "$out/com.android.adbd:/system/apex/com.android.adbd:ro" \
  -v "$here/dmaheap-shim/libdmaheap_shim.so:/system/lib64/libdmaheap_shim.so:ro"
# the patched binaries read values entry.sh only writes when the host needs it
if [ -f "$out/init.patched" ] && [ ! -e /proc/sys/vm/mmap_rnd_compat_bits ]; then
  set -- "$@" -v "$out/init.patched:/system/bin/init:ro"
fi
if [ -f "$out/bpfloader.patched" ] &&
   [ "$(cat /proc/sys/kernel/unprivileged_bpf_disabled 2>/dev/null)" = 1 ]; then
  set -- "$@" -v "$out/bpfloader.patched:/system/bin/bpfloader:ro"
fi
exec docker run -d "$@" --entrypoint /.nk/out/busybox "$img" \
  sh /.nk/entry.sh qemu=1 androidboot.hardware=redroid androidboot.use_memfd=1
