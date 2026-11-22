# Webcam for redroid

Optional layer on top of a stock redroid image: Android gets a front camera fed
by the webcam of a browser connected to `redroid-webrtc`. Tested on Android 14 and 16.

```bash
docker build -t redroid/redroid:14.0.0_64only-camera .
```

For Android 16:

```bash
docker build -t redroid/redroid:16.0.0_64only-camera \
  --build-arg REDROID_IMAGE=redroid/redroid:16.0.0_64only-latest \
  --build-arg CAMERA_METADATA_LIB=/system/lib64/libcamera_metadata.so .
```

The host kernel has to run stock redroid 16 first. Android 16 needs ashmem or an
ashmem-on-memfd shim and the BPF JIT; a kernel without ashmem (Ubuntu 24.04's, for
example) fails before any camera code runs, with "Failed to create ashmem". On
such a kernel the out-of-tree ashmem module from
[redroid-modules](https://github.com/remote-android/redroid-modules) works once it
is patched for 6.x, and SurfaceFlinger also needs `service.sf.prime_shader_cache`
set to `false` (the stock 16 image aborts in its shader priming without a GPU).

Use that image for the device, and share its `/ipc` with `redroid-webrtc` (the
`docker-compose.yml` of `webrtc/` already does). On the device page, turn on the
camera button and allow the browser to use the webcam. Without a webcam, apps
see a moving test pattern.

| Piece | What it does |
|---|---|
| `src/main.cpp` | AIDL camera provider (external level, 1280x720 and below, YUV, JPEG). Reads I420 frames from `/ipc/camera.sock`. |
| `src/gralloc_wrap.c` | Replaces `gralloc.redroid.so`: adds the YUV and BLOB buffers cameras need, which redroid's gralloc doesn't have, and keeps the original for the rest. |
| `etc/` | Service definition, VINTF entry and camera feature. |

It builds with the NDK's sysroot and the public AIDL files, without an AOSP tree.
The camera behaves like a phone's in portrait: the scene is cropped to the
portrait view, and frames are not rotated with the device. Photos are not
mirrored, unlike the preview.

## Known limits

- Recording uses the default camcorder profile (352x288), because redroid has no
  `media_profiles.xml`.
- Software-decoded video can't be shown on screen, with or without this layer:
  redroid's gralloc doesn't support YUV buffers for the display.
- On arm64 hosts the build stage needs amd64 emulation, which doesn't survive a
  reboot: `docker run --rm --privileged tonistiigi/binfmt --install amd64`.
