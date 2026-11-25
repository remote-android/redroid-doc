# Showing Android the container's own memory and CPUs (lxcfs)

A container shares `/proc` with the host. So even with `--memory` or `--cpuset-cpus`, Android inside redroid reads the **host's** total memory, CPU count and uptime. A 2 GB instance on a 64 GB host reports `Total RAM: 64 GB`. Android then sizes its caches and its low-memory behaviour for memory it doesn't have, and the cgroup OOM killer ends up doing the work abruptly.

[lxcfs](https://github.com/lxc/lxcfs) is a small FUSE daemon that serves per-container versions of these files, computed from the reading process's own cgroup.

## Why the usual `-v /var/lib/lxcfs/proc/meminfo:/proc/meminfo` does not work

Android's init mounts a fresh `procfs` over `/proc` early in boot, which hides any bind that Docker put on `/proc/...`. The bind has to be made by Android's own init **after** that remount. The steps below do that with a small `.rc` file at `post-fs-data`.

## Steps

1. Install and start lxcfs on the host:

   ```bash
   sudo apt install lxcfs        # Debian/Ubuntu; other distros ship it too
   sudo systemctl enable --now lxcfs
   ls /var/lib/lxcfs/proc        # meminfo, cpuinfo, stat, uptime, ...
   ```

2. Create `redroid-lxcfs.rc` with mode **0644**. Android's init skips an `.rc` file that is group- or world-writable ("Skipping insecure file").

   ```
   on post-fs-data
       mount none /lxcfs/proc/meminfo /proc/meminfo bind
       mount none /lxcfs/proc/cpuinfo /proc/cpuinfo bind
       mount none /lxcfs/proc/stat /proc/stat bind
       mount none /lxcfs/proc/uptime /proc/uptime bind
       mount none /lxcfs/proc/loadavg /proc/loadavg bind
       mount none /lxcfs/proc/swaps /proc/swaps bind
       mount none /lxcfs/proc/diskstats /proc/diskstats bind
   ```

   ```bash
   chmod 644 redroid-lxcfs.rc
   ```

3. Run redroid with lxcfs mounted **outside** `/proc` and the `.rc` in `/vendor/etc/init`:

   ```bash
   docker run -itd --rm --privileged \
       --memory 2g --memory-swap 2g --cpuset-cpus 2,10 \
       -v /var/lib/lxcfs:/lxcfs:rslave \
       -v $PWD/redroid-lxcfs.rc:/vendor/etc/init/redroid-lxcfs.rc:ro \
       -v ~/data:/data \
       -p 5555:5555 \
       redroid/redroid:15.0.0-latest
   ```

   You could also build the `.rc` into an image with a `Dockerfile`, the same way as in [Native Bridge Support](README.md#native-bridge-support).

4. Check it from inside the container:

   ```bash
   adb shell grep MemTotal /proc/meminfo           # the container's limit
   adb shell dumpsys meminfo | grep "Total RAM"
   adb shell grep -c ^processor /proc/cpuinfo      # the container's cpuset
   ```

## Results

Tested on an AMD Ryzen 7 5700G host (64 GB, 16 threads, Ubuntu 24.04, cgroup v2, lxcfs 5.0.4) with `redroid/redroid:15.0.0-latest`, `--memory 2g --cpuset-cpus 2,10`:

| | Without lxcfs | With lxcfs + the `.rc` |
|---|---|---|
| `MemTotal` | 63,646,788 kB | 2,097,152 kB |
| `dumpsys meminfo` Total RAM | 63.6 GB | 2.0 GB |
| processors in `/proc/cpuinfo` | 16 | 2 |
| `/proc/uptime` | host uptime (days) | container uptime (seconds) |

## Notes

- **If the lxcfs daemon restarts** (for example after a package upgrade), the FUSE mount goes stale inside running containers. Restart the containers.
- **The `rslave` propagation** on the `/lxcfs` volume lets a remount of lxcfs on the host reach the container.
- **Two things lxcfs does not cover:** the CPU count Android reads from `/sys/devices/system/cpu/{online,possible,present}`, and the memory pressure lmkd reads from `/proc/pressure/memory`. Both are handled inside the image by remote-android/vendor_redroid#6, which needs no host tool.
