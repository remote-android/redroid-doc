#!/.nk/out/busybox sh
# Wrapper entrypoint for redroid on kernels without Android-specific features
# (e.g. OrbStack, cloud kernels). Runs before Android's /init, using a static
# busybox because bionic's linker is not usable until init sets up APEXes.
#
#   started by run.sh as: docker run ... --entrypoint /.nk/out/busybox \
#     redroid/redroid:<ver> sh /.nk/entry.sh <init args>
#
# Fixes, each applied only when the kernel lacks the feature:
#   1. no PSI (/proc/pressure) -> lmkd exits, boot stalls at FallbackHome.
#      Fake the legacy in-kernel LMK sysfs interface so lmkd runs in
#      "in-kernel" mode (it then only writes minfree/adj; nothing is killed).
#   2. no /proc/sys/vm/mmap_rnd_compat_bits (no CONFIG_COMPAT) -> Android 12/13
#      init aborts in SetMmapRndBits. Provide the value at /dev/.nk-vm/ and
#      run.sh mounts out/<ver>/init.patched over /system/bin/init.
#   3. no /dev/dma_heap -> create /dev/dma_heap/system as a /dev/null node so
#      Codec2 is enabled; libdmaheap_shim.so (LD_PRELOAD) does the allocation.
#      Only when NK_DMAHEAP=1 (needs the shim, else codecs fail outright).
#   4. kernel.unprivileged_bpf_disabled locked to 1 -> bpfloader fails and init
#      reboots. Provide a writable copy at /dev/.nk-vm/; run.sh mounts
#      out/<ver>/bpfloader.patched over /system/bin/bpfloader.
set -u
bb=/.nk/out/busybox

if [ ! -e /proc/pressure/memory ] && [ ! -d /sys/module/lowmemorykiller ]; then
  $bb mount -t tmpfs -o mode=0755 nk-sysmodule /sys/module
  $bb mkdir -p /sys/module/lowmemorykiller/parameters
  echo "0,100,200,250,900,950" > /sys/module/lowmemorykiller/parameters/adj
  echo "18432,23040,27648,32256,55296,80640" > /sys/module/lowmemorykiller/parameters/minfree
  $bb chmod 0666 /sys/module/lowmemorykiller/parameters/adj /sys/module/lowmemorykiller/parameters/minfree
  echo "nokernel: faked in-kernel lowmemorykiller (no PSI)" > /dev/kmsg
fi

if [ ! -e /proc/sys/vm/mmap_rnd_compat_bits ]; then
  # init remounts /proc, so a fake under /proc/sys/vm would be hidden;
  # patched init (patch-binaries.py) reads this /dev path instead.
  $bb mkdir -p /dev/.nk-vm
  echo 16 > /dev/.nk-vm/mmap_rnd_compat_bits
  echo "nokernel: fake mmap_rnd_compat_bits at /dev/.nk-vm (no CONFIG_COMPAT)" > /dev/kmsg
fi

if [ "$($bb cat /proc/sys/kernel/unprivileged_bpf_disabled 2>/dev/null)" = 1 ]; then
  # locked by the host (e.g. Alpine's sysctl defaults): bpfloader's write of 0
  # fails and init reboots. The patched bpfloader writes here instead.
  $bb mkdir -p /dev/.nk-vm
  echo 1 > /dev/.nk-vm/unprivileged_bpf_disabled
  echo "nokernel: unprivileged_bpf_disabled locked by host, using /dev/.nk-vm" > /dev/kmsg
fi

if [ "${NK_DMAHEAP:-0}" = 1 ] && [ ! -e /dev/dma_heap/system ]; then
  # the shim's preferred path: real dma-bufs from memfds (CONFIG_UDMABUF);
  # media.swcodec / app processes are unprivileged
  [ -c /dev/udmabuf ] && $bb chmod 0666 /dev/udmabuf
  $bb mkdir -p /dev/dma_heap
  $bb mknod -m 0444 /dev/dma_heap/system c 1 3
  echo "nokernel: placeholder /dev/dma_heap/system for libdmaheap_shim" > /dev/kmsg
fi

exec /init "$@"
