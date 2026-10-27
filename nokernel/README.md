# Running redroid on kernels without dma_heap / PSI / CONFIG_COMPAT

Besides binder, redroid quietly depends on a few kernel features (and one
sysctl) that many Docker hosts don't have. On those hosts the container either
exits right away, sits on a black `FallbackHome` screen forever (14), or boots
but has no video encoders (`List of video encoders: (none)`, see #407).

This directory works around all of these from userspace, with plain
`docker run` flags and no image rebuild. **Binder is still required.**

Tested with `redroid/redroid:{12,13,14}.0.0_64only-latest` (arm64) on:

| Host kernel | binder | PSI | COMPAT | dma_heap | Result |
|---|---|---|---|---|---|
| OrbStack (macOS, 7.0) | built in | no | no | no | stock image: 12/13 exit, 14 never finishes booting. With nokernel: boots in 6–10 s, scrcpy H.264 + Opus works |
| Debian 13 `linux-image-arm64` (6.12) | `binder_linux` module | yes | yes | no | stock image boots but has no encoders. With nokernel: scrcpy H.264 + Opus works |
| Debian 13 `linux-image-cloud-arm64` | none | yes | yes | no | doesn't work (no binder); install `linux-image-arm64` |
| Alpine 3.23 `linux-lts` (6.18) | built in, binderfs | off by default | yes | no | stock 14 exits (BPF sysctl locked, see below). With nokernel: 12 and 14 boot, scrcpy H.264 + Opus works. 13 crash-loops in system_server here, with or without nokernel |
| Alpine `linux-virt` | none | — | — | — | doesn't work (no binder); install `linux-lts` |

## What breaks and why

| Missing feature | Symptom | Workaround here |
|---|---|---|
| PSI (`CONFIG_PSI`, `/proc/pressure`) and memcg v1 | `lmkd` exits with "Kernel does not support memory pressure events or in-kernel low memory killer", init restarts it forever and `sys.boot_completed` is never set: black screen / `FallbackHome` | `entry.sh` mounts a tmpfs on `/sys/module` with fake `lowmemorykiller/parameters/{minfree,adj}`. lmkd then runs in legacy "in-kernel" mode and never kills anything itself; use `--memory` so the cgroup OOM killer handles pressure |
| `CONFIG_COMPAT` (`/proc/sys/vm/mmap_rnd_compat_bits`), common on arm64 VM kernels | Android 12/13 init: "Unable to set adequate mmap entropy value!", container exits with 129 within a second (14 already skips this when there is no 32-bit ABI) | `patch-binaries.py` makes a copy of init read the value from `/dev/.nk-vm/` (same-length string patch); `/proc` can't be faked because init mounts a fresh procfs |
| `kernel.unprivileged_bpf_disabled` locked to `1` (Alpine's default sysctl; the kernel refuses changes until reboot) | Android 14 `bpfloader` fails to write `0` and is `reboot_on_failure`: container exits with 129 (12/13 only try from an rc file and carry on) | `patch-binaries.py` points a copy of bpfloader at `/dev/.nk-vm/`, so the host keeps unprivileged BPF disabled. Alternatively set it to `2` on the host (Ubuntu/Debian default), which lets Android flip it to `0` host-wide |
| DMA-BUF heaps (`CONFIG_DMABUF_HEAPS_SYSTEM`) | `redroid.c2.sh` keeps Codec2 off, so there are no H.264/Opus encoders; forcing it on makes allocations fail | `dmaheap-shim/`: an `LD_PRELOAD` library that answers `DMA_HEAP_IOCTL_ALLOC` itself, with a real dma-buf from a memfd via `/dev/udmabuf` when available, otherwise with a plain memfd |
| ashmem | nothing, with `androidboot.use_memfd=1` (run.sh sets it) | — |

### The dma_heap shim

`/dev/dma_heap/system` is created as a `/dev/null` node, so libdmabufheap can
open it and `redroid.c2.sh` turns Codec2 on. The shim exports `ioctl()`; every
call goes to the kernel first, and only when the kernel answers `ENOTTY` to
`DMA_HEAP_IOCTL_ALLOC` does the shim allocate the buffer:

1. `memfd_create` + seal + `UDMABUF_CREATE` on `/dev/udmabuf` → a real dma-buf;
2. if udmabuf is missing or not accessible (OrbStack blocks it), the sealed
   memfd itself. `DMA_BUF_IOCTL_SYNC` / `DMA_BUF_SET_NAME` on such a buffer are
   answered with success, since CPU-only memory is always coherent.

On kernels with real dma heaps nothing changes. The library is freestanding
(raw syscalls, only `__errno` imported), so it builds with stock clang + lld
and no NDK.

It is preloaded via `setenv LD_PRELOAD` into two services:
`media.swcodec` (the codec side) and `adbd`, so `adb shell` children such as
scrcpy's server get it too. Exporting `LD_PRELOAD` globally from init does not
work: bionic treats an unresolvable preload as fatal and processes like
`linkerconfig` die before the library is visible to them.

## Usage

Host prerequisites: Docker, Python 3, and binder.

```bash
# OrbStack: binder is built in, but the nodes are root-only after each start
docker run --rm --privileged --pid=host alpine nsenter -t 1 -m -- chmod 666 /dev/binder /dev/hwbinder /dev/vndbinder

# Debian (standard kernel, not the cloud one)
sudo modprobe binder_linux devices=binder,hwbinder,vndbinder && sudo chmod 666 /dev/*binder

# Alpine: use linux-lts (binder and binderfs are built in, redroid mounts binderfs itself)
apk add linux-lts && apk del linux-virt && grub-mkconfig -o /boot/grub/grub.cfg && reboot
```

Then:

```bash
./prepare.sh 14            # builds the shim, fetches a static busybox, extracts/patches files into out/14
./run.sh 14 redroid14 5555
adb connect localhost:5555
scrcpy -s localhost:5555
```

`entry.sh` only applies the workarounds the running kernel actually needs, so
the same command works on hosts that have PSI or COMPAT. Use `REDROID_IMAGE`
to point at a different image tag.

## Limitations

- redroid 13 crash-loops in system_server on Alpine's `linux-lts` 6.18 even
  without nokernel (12 and 14 are fine there, and 13 is fine on Debian 6.12
  and OrbStack 7.0); not investigated yet.
- Binder has no userspace replacement. Docker Desktop's LinuxKit kernel and
  Debian's cloud kernels don't have it; use a host or VM with a kernel that does.
- arm64 only for now (the shim uses arm64 syscall numbers).
- Apps that use MediaCodec with byte buffers allocate in their own process and
  would need the shim in zygote as well; not covered yet. Surface-input
  encoding (scrcpy video) and anything started from `adb shell` are covered.
- memfd fallback buffers can't be imported by a GPU. That's irrelevant in the
  default guest (SwiftShader) mode.
- Without PSI, Android never kills cached apps on its own; set `--memory`.

## Possible image-side fixes

These would make most of this directory unnecessary:

- In `init.redroid.rc`, set up the fake lowmemorykiller files when
  `/proc/pressure` is missing, before lmkd starts.
- Backport the "skip `mmap_rnd_compat_bits` without a 32-bit ABI" check into
  the 12/13 init.
- Don't treat a failed `unprivileged_bpf_disabled` write as fatal in bpfloader.
- Make Codec2 independent of dma heaps: `debug.stagefright.c2-poolmask=0xB0000`
  already switches linear buffers to the gralloc BLOB allocator, but
  `gralloc.redroid` rejects `HAL_PIXEL_FORMAT_BLOB` today. Adding BLOB support
  there (or a memfd fallback in libdmabufheap) would remove the need for the
  shim.
