/*
 * Copyright (C) 2026 The redroid Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Replaces gralloc.redroid.so to add the buffers cameras need. redroid's
// gralloc only allocates RGB, so BLOB (JPEG) and YCbCr_420_888 buffers are
// carried in larger RGBA buffers. A YCbCr buffer starts with a header, because
// a gralloc0 handle cannot tell other processes its layout. Everything else
// goes to the original module.

#include <dlfcn.h>
#include <errno.h>
#include <hardware/gralloc.h>
#include <android/log.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>

#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, "redroid-gralloc", __VA_ARGS__)
#define REAL_PATH "/vendor/lib64/hw/gralloc.redroid-real.so"
#define YUV_MAGIC 0x52445955
#define YUV_HEADER 64
#define ALIGN(v, a) (((v) + (a)-1) / (a) * (a))

struct yuv_header {
  uint32_t magic, stride, height;
};

static gralloc_module_t* real;
static int (*real_alloc)(alloc_device_t*, int, int, int, int, buffer_handle_t*, int*);

static void load_real(void) {
  void* lib = dlopen(REAL_PATH, RTLD_NOW);
  if (lib) real = dlsym(lib, HAL_MODULE_INFO_SYM_AS_STR);
  if (!real) ALOGE("gralloc wrapper: cannot load %s: %s", REAL_PATH, dlerror());
}
static int ensure_real(void) {
  static pthread_once_t once = PTHREAD_ONCE_INIT;
  pthread_once(&once, load_real);
  return real ? 0 : -ENODEV;
}

static int write_header(buffer_handle_t handle, int usage, const struct yuv_header* h) {
  void* base = NULL;
  int registered = 0;
  int result = real->lock(real, handle, GRALLOC_USAGE_SW_WRITE_OFTEN, 0, 0, 1, 1, &base);
  if (result != 0) {
    // The allocating process may need to map the buffer first.
    if ((result = real->registerBuffer(real, handle)) != 0) return result;
    registered = 1;
    result = real->lock(real, handle, GRALLOC_USAGE_SW_WRITE_OFTEN, 0, 0, 1, 1, &base);
  }
  if (result == 0) {
    memcpy(base, h, sizeof(*h));
    real->unlock(real, handle);
  }
  if (registered) real->unregisterBuffer(real, handle);
  return result;
}

static int wrap_alloc(alloc_device_t* dev, int w, int h, int format, int usage,
                      buffer_handle_t* handle, int* stride) {
  usage |= GRALLOC_USAGE_SW_WRITE_OFTEN | GRALLOC_USAGE_SW_READ_OFTEN;
  if (format == HAL_PIXEL_FORMAT_BLOB) {
    int unused;
    int result = real_alloc(dev, (w + 3) / 4, h, HAL_PIXEL_FORMAT_RGBA_8888, usage, handle, &unused);
    if (result == 0) *stride = w;
    return result;
  }
  if (format == HAL_PIXEL_FORMAT_YCbCr_420_888) {
    int rgba_stride, width = ALIGN(w, 16) / 4;
    // 1.5 rows of 8 bit samples per pixel row, then room for the header. The
    // rows of the RGBA buffer become the rows of the Y plane.
    int height = h + (h + 1) / 2 + 2 + YUV_HEADER / (width * 4);
    int result = real_alloc(dev, width, height, HAL_PIXEL_FORMAT_RGBA_8888, usage, handle, &rgba_stride);
    if (result != 0) return result;
    struct yuv_header header = {YUV_MAGIC, (uint32_t)rgba_stride * 4, (uint32_t)h};
    if ((result = write_header(*handle, usage, &header)) != 0) {
      dev->free(dev, *handle);
      return result;
    }
    *stride = rgba_stride * 4;
    return 0;
  }
  return real_alloc(dev, w, h, format, usage, handle, stride);
}

static int wrap_open(const hw_module_t* module, const char* name, hw_device_t** device) {
  if (ensure_real() != 0) return -ENODEV;
  int result = real->common.methods->open(&real->common, name, device);
  if (result == 0 && strcmp(name, GRALLOC_HARDWARE_GPU0) == 0) {
    alloc_device_t* alloc = (alloc_device_t*)*device;
    real_alloc = alloc->alloc;
    alloc->alloc = wrap_alloc;
  }
  return result;
}

static int wrap_register(const gralloc_module_t* m, buffer_handle_t h) {
  return ensure_real() ? -ENODEV : real->registerBuffer(real, h);
}
static int wrap_unregister(const gralloc_module_t* m, buffer_handle_t h) {
  return real->unregisterBuffer(real, h);
}
static int wrap_lock(const gralloc_module_t* m, buffer_handle_t h, int usage, int l, int t, int w,
                     int height, void** vaddr) {
  return real->lock(real, h, usage, 0, 0, 1, 1, vaddr);
}
static int wrap_unlock(const gralloc_module_t* m, buffer_handle_t h) {
  return real->unlock(real, h);
}
static int wrap_lock_ycbcr(const gralloc_module_t* m, buffer_handle_t h, int usage, int l, int t,
                           int w, int height, struct android_ycbcr* ycbcr) {
  void* base = NULL;
  int result = real->lock(real, h, usage | GRALLOC_USAGE_SW_READ_OFTEN, 0, 0, 1, 1, &base);
  if (result != 0) return result;
  const struct yuv_header* header = base;
  if (header->magic != YUV_MAGIC) {
    real->unlock(real, h);
    return -EINVAL;
  }
  uint8_t* y = (uint8_t*)base + YUV_HEADER;
  size_t cstride = header->stride / 2;
  uint8_t* cb = y + header->stride * (size_t)header->height;
  memset(ycbcr, 0, sizeof(*ycbcr));
  ycbcr->y = y;
  ycbcr->cb = cb;
  ycbcr->cr = cb + cstride * ((header->height + 1) / 2);
  ycbcr->ystride = header->stride;
  ycbcr->cstride = cstride;
  ycbcr->chroma_step = 1;
  return 0;
}

static struct hw_module_methods_t wrap_methods = {.open = wrap_open};

__attribute__((visibility("default"))) gralloc_module_t HAL_MODULE_INFO_SYM = {
    .common = {.tag = HARDWARE_MODULE_TAG,
               .version_major = 0,
               .version_minor = 2,  // GRALLOC_MODULE_API_VERSION_0_2: lock_ycbcr
               .id = GRALLOC_HARDWARE_MODULE_ID,
               .name = "redroid gralloc with camera formats",
               .author = "The redroid Authors",
               .methods = &wrap_methods},
    .registerBuffer = wrap_register,
    .unregisterBuffer = wrap_unregister,
    .lock = wrap_lock,
    .unlock = wrap_unlock,
    .lock_ycbcr = wrap_lock_ycbcr,
};
