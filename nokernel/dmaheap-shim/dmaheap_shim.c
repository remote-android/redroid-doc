// dmaheap_shim: LD_PRELOAD library that emulates DMA-BUF heaps
// (/dev/dma_heap/<name>, CONFIG_DMABUF_HEAPS) for Android userspace on kernels
// that lack them (e.g. OrbStack, WSL2, many cloud/Docker Desktop kernels).
//
// /dev/dma_heap/system must exist as *some* openable char node so that
// libdmabufheap can open it (and redroid.c2.sh's `[ -c ... ]` check enables
// Codec2). /dev/null (1:3) works. When a process issues DMA_HEAP_IOCTL_ALLOC
// and the kernel answers ENOTTY, the shim allocates the buffer itself:
//
//   1. memfd + UDMABUF_CREATE on /dev/udmabuf  -> a genuine dma-buf fd
//      (needs CONFIG_UDMABUF and access to /dev/udmabuf)
//   2. otherwise a plain sealed memfd, handed out as if it were a dma-buf.
//      mmap/fd passing work identically. The dma-buf-only ioctls
//      (DMA_BUF_IOCTL_SYNC, DMA_BUF_SET_NAME) then fail with ENOTTY on that fd;
//      the shim reports success for them, since CPU-only memfd memory is
//      always coherent.
//
// On kernels with real dma heaps every ioctl passes straight through.
//
// Freestanding: no libc headers, raw syscalls only, so it builds with any
// clang + lld for aarch64 without the NDK. Only __errno() is imported.

typedef unsigned long u64;
typedef unsigned int u32;

#define __NR_fcntl 25
#define __NR_ioctl 29
#define __NR_ftruncate 46
#define __NR_openat 56
#define __NR_close 57
#define __NR_memfd_create 279

#define AT_FDCWD (-100)
#define ENOTTY 25
#define EINVAL 22
#define F_ADD_SEALS 1033
#define F_SEAL_SHRINK 0x0002
#define F_SEAL_GROW 0x0004
#define MFD_CLOEXEC 0x0001U
#define MFD_ALLOW_SEALING 0x0002U
#define O_RDWR 02
#define O_CLOEXEC 02000000

// <linux/dma-heap.h>
struct dma_heap_allocation_data {
  u64 len;
  u32 fd;
  u32 fd_flags;
  u64 heap_flags;
};
#define DMA_HEAP_IOCTL_ALLOC 0xc0184800u  // _IOWR('H', 0x0, 24 bytes)

// <linux/dma-buf.h>
#define DMA_BUF_IOCTL_SYNC 0x40086200u    // _IOW('b', 0, u64)
#define DMA_BUF_SET_NAME_A 0x40046201u    // _IOW('b', 1, u32)
#define DMA_BUF_SET_NAME_B 0x40086201u    // _IOW('b', 1, u64)

// <linux/udmabuf.h>
struct udmabuf_create {
  u32 memfd;
  u32 flags;
  u64 offset;
  u64 size;
};
#define UDMABUF_FLAGS_CLOEXEC 0x01
#define UDMABUF_CREATE 0x40187542u  // _IOW('u', 0x42, 24 bytes)

extern int* __errno(void);

static long sys4(long n, long a, long b, long c, long d) {
  register long x8 __asm__("x8") = n;
  register long x0 __asm__("x0") = a;
  register long x1 __asm__("x1") = b;
  register long x2 __asm__("x2") = c;
  register long x3 __asm__("x3") = d;
  __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3) : "memory");
  return x0;
}
#define sys3(n, a, b, c) sys4(n, a, b, c, 0)

static int ret_errno(long r) {
  if (r < 0 && r > -4096) {
    *__errno() = (int)-r;
    return -1;
  }
  return (int)r;
}

// -1 = not probed yet, -2 = unavailable, otherwise an fd to /dev/udmabuf.
// Racy first probe is harmless: worst case one extra fd is opened.
static volatile int g_udmabuf = -1;

static int udmabuf_fd(void) {
  int fd = g_udmabuf;
  if (fd == -1) {
    long r = sys4(__NR_openat, AT_FDCWD, (long)"/dev/udmabuf", O_RDWR | O_CLOEXEC, 0);
    fd = r < 0 ? -2 : (int)r;
    g_udmabuf = fd;
  }
  return fd;
}

static long emulate_alloc(struct dma_heap_allocation_data* d) {
  u64 size = (d->len + 4095) & ~4095ULL;
  if (size == 0) return -EINVAL;
  u32 mfd_flags = MFD_ALLOW_SEALING | ((d->fd_flags & O_CLOEXEC) ? MFD_CLOEXEC : 0);
  long memfd = sys3(__NR_memfd_create, (long)"dmabuf-heap", mfd_flags, 0);
  if (memfd < 0) return memfd;
  long r = sys3(__NR_ftruncate, memfd, (long)size, 0);
  if (r < 0) goto fail;
  r = sys3(__NR_fcntl, memfd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW);
  if (r < 0) goto fail;

  int ufd = udmabuf_fd();
  if (ufd >= 0) {
    struct udmabuf_create c = {(u32)memfd,
                               (d->fd_flags & O_CLOEXEC) ? UDMABUF_FLAGS_CLOEXEC : 0,
                               0, size};
    r = sys3(__NR_ioctl, ufd, UDMABUF_CREATE, (long)&c);
    if (r >= 0) {
      sys3(__NR_close, memfd, 0, 0);  // the dma-buf holds its own reference
      d->fd = (u32)r;
      return 0;
    }
  }
  d->fd = (u32)memfd;  // fallback: plain memfd
  return 0;

fail:
  sys3(__NR_close, memfd, 0, 0);
  return r;
}

// bionic: int ioctl(int fd, int request, ...)
__attribute__((visibility("default"))) int ioctl(int fd, int request, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, request);
  void* arg = __builtin_va_arg(ap, void*);
  __builtin_va_end(ap);

  u32 req = (u32)request;
  long r = sys3(__NR_ioctl, fd, (long)req, (long)arg);
  if (r == -ENOTTY) {
    if (req == DMA_HEAP_IOCTL_ALLOC)
      r = emulate_alloc((struct dma_heap_allocation_data*)arg);
    else if (req == DMA_BUF_IOCTL_SYNC || req == DMA_BUF_SET_NAME_A ||
             req == DMA_BUF_SET_NAME_B)
      r = 0;  // memfd fallback buffer: CPU-coherent, names are cosmetic
  }
  return ret_errno(r);
}
