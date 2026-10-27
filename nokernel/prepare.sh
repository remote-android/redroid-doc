#!/bin/sh
# Prepare the files run.sh mounts into a redroid container.
#
#   ./prepare.sh 14
#
# Produces (all under ./out, nothing is committed):
#   out/busybox                  static busybox used by entry.sh before /init
#   dmaheap-shim/libdmaheap_shim.so
#   out/<ver>/com.android.media.swcodec, out/<ver>/com.android.adbd
#       APEX directories copied from the image, with the shim in lib64/ and
#       `setenv LD_PRELOAD libdmaheap_shim.so` appended to their services.
#       Whole directories are mounted because apexd bind-mounts
#       /system/apex/<name> non-recursively, which hides single-file mounts.
#   out/<ver>/init.patched       (Android 12/13 only)
#   out/<ver>/bpfloader.patched  (see patch-binaries.py)
#
# Only arm64 images are supported for now (the shim uses arm64 syscalls).
set -eu
v=$1
img=${REDROID_IMAGE:-redroid/redroid:$v.0.0_64only-latest}
here=$(cd "$(dirname "$0")" && pwd)
out=$here/out/$v
shim=$here/dmaheap-shim/libdmaheap_shim.so
mkdir -p "$out"

if [ ! -f "$shim" ]; then
  docker run --rm -v "$here/dmaheap-shim:/src" -w /src alpine:3.20 sh build.sh
fi
if [ ! -f "$here/out/busybox" ]; then
  docker run --rm -v "$here/out:/out" busybox:1.36.1-musl cp /bin/busybox /out/busybox
fi

cid=$(docker create "$img")
trap 'docker container rm "$cid" >/dev/null' EXIT
for apex in com.android.media.swcodec com.android.adbd; do
  d=$out/$apex
  [ -d "$d" ] && continue
  docker cp "$cid:/system/apex/$apex" "$d"
  cp "$shim" "$d/lib64/"
  for rc in "$d"/etc/*rc; do
    [ -f "$rc" ] || continue
    grep -q '^service ' "$rc" || continue
    # the service block is the last section in these files
    printf '    setenv LD_PRELOAD libdmaheap_shim.so\n' >> "$rc"
  done
done
if [ "$v" -le 13 ] && [ ! -f "$out/init.patched" ]; then
  docker cp -L "$cid:/system/bin/init" "$out/init.orig"
  python3 -I "$here/patch-binaries.py" init "$out/init.orig" "$out/init.patched"
  chmod 755 "$out/init.patched"
fi
if [ ! -f "$out/bpfloader.orig" ]; then
  docker cp -L "$cid:/system/bin/bpfloader" "$out/bpfloader.orig"
  python3 -I "$here/patch-binaries.py" bpfloader "$out/bpfloader.orig" "$out/bpfloader.patched"
  [ -f "$out/bpfloader.patched" ] && chmod 755 "$out/bpfloader.patched"
fi
echo "prepared $out"
