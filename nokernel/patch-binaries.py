# Same-length in-place path patches for Android binaries, so they read or
# write a file under /dev/.nk-vm/ (created by entry.sh) instead of a /proc
# file the host kernel doesn't provide or doesn't let containers change.
#
#   python3 patch-binaries.py <init|bpfloader> <in> <out>
#
# init (12/13):  /proc/sys/vm/mmap_rnd_compat_bits is missing without
#                CONFIG_COMPAT and init aborts in SetMmapRndBits.
# bpfloader:     writing 0 to /proc/sys/kernel/unprivileged_bpf_disabled fails
#                when the host has locked it to 1 (Alpine's default sysctl),
#                and init reboots because bpfloader is reboot_on_failure.
import sys

PATCHES = {
    "init": (b"/proc/sys/vm/mmap_rnd_compat_bits\0",
             b"/dev/.nk-vm//mmap_rnd_compat_bits\0"),
    "bpfloader": (b"/proc/sys/kernel/unprivileged_bpf_disabled\0",
                  b"/dev/.nk-vm//////unprivileged_bpf_disabled\0"),
}

kind, src_path, dst_path = sys.argv[1:4]
old, new = PATCHES[kind]
assert len(old) == len(new)
src = open(src_path, "rb").read()
n = src.count(old)
if n == 0:  # e.g. bpfloader before Android 14 doesn't touch the sysctl
    print(f"{src_path}: {old[:-1].decode()} not used, no patch needed")
    sys.exit(0)
if n != 1:
    sys.exit(f"{src_path}: {n} occurrences of {old[:-1].decode()}, not patching")
open(dst_path, "wb").write(src.replace(old, new))
