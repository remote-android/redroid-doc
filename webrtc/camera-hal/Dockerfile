# Stock redroid plus a camera fed by a browser's webcam. See README.md.
#   docker build -t redroid/redroid:14.0.0_64only-camera .
ARG REDROID_IMAGE=redroid/redroid:14.0.0_64only-latest
ARG UBUNTU=ubuntu:24.04
ARG AOSP_TAG=android-14.0.0_r1
FROM ${REDROID_IMAGE} AS stock

# Always amd64: Google ships the NDK and aidl for that host only. They
# cross-compile for both target architectures (emulated on arm64 hosts).
FROM --platform=linux/amd64 ${UBUNTU} AS build
ARG TARGETARCH
ARG AOSP_TAG
ARG NDK_SHA256=59c2f6dc96743b5daf5d1626684640b20a6bd2b1d85b13156b90333741bad5cc
ARG BUILD_TOOLS_SHA256=c57932465d8dbb3f58511c8f576fb15057a3e1b5c92af164b596edd9bcded375
ARG STB_COMMIT=5c205738c191bcb0abc65c4febfa9bd25ff35234
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates curl unzip && rm -rf /var/lib/apt/lists/*
RUN R=https://dl.google.com/android/repository \
    && curl -fsSL -o /tmp/ndk.zip $R/android-ndk-r27c-linux.zip \
    && echo "${NDK_SHA256}  /tmp/ndk.zip" | sha256sum -c \
    && unzip -q /tmp/ndk.zip -d /tmp/ndk && mv /tmp/ndk/android-ndk-r27c /ndk \
    && curl -fsSL -o /tmp/bt.zip $R/build-tools_r34-rc3-linux.zip \
    && echo "${BUILD_TOOLS_SHA256}  /tmp/bt.zip" | sha256sum -c \
    && unzip -qjo /tmp/bt.zip '*/aidl' -d /usr/local/bin && chmod +x /usr/local/bin/aidl \
    && unzip -qjo /tmp/bt.zip '*/lib64/libc++.so*' -d /usr/local/lib && ldconfig \
    && rm -rf /tmp/ndk* /tmp/bt.zip
RUN A="https://android.googlesource.com/platform" && mkdir -p /src/interfaces /src/headers/android \
      /src/headers/system /src/headers/hardware /src/headers/cutils /src/stb \
    && curl -fsSL "$A/hardware/interfaces/+archive/refs/tags/${AOSP_TAG}.tar.gz" \
       | tar -xz -C /src/interfaces camera/provider/aidl camera/device/aidl camera/common/aidl \
           camera/metadata/aidl graphics/common/aidl common/aidl common/fmq/aidl \
    && get() { curl -fsSL "$A/$1/+/refs/tags/${AOSP_TAG}/$2?format=TEXT" | base64 -d > "/src/headers/$3"; } \
    && for f in binder_manager binder_process binder_stability; do \
         get frameworks/native libs/binder/ndk/include_platform/android/$f.h android/$f.h; done \
    && for f in camera_metadata camera_metadata_tags camera_vendor_tags; do \
         get system/media camera/include/system/$f.h system/$f.h; done \
    && for f in hardware gralloc fb; do \
         get hardware/libhardware include/hardware/$f.h hardware/$f.h; done \
    && for f in graphics graphics-base graphics-base-v1.0 graphics-base-v1.1 graphics-base-v1.2 graphics-sw; do \
         get system/core libsystem/include/system/$f.h system/$f.h; done \
    && get system/core libcutils/include/cutils/native_handle.h cutils/native_handle.h \
    && curl -fsSL -o /src/stb/stb_image_write.h \
       "https://raw.githubusercontent.com/nothings/stb/${STB_COMMIT}/stb_image_write.h" \
    && printf '#pragma once\n#define ANDROID_API __attribute__((visibility("default")))\n' \
       > /src/headers/cutils/compiler.h
COPY --from=stock /system/apex/com.android.vndk.current/lib64/libcamera_metadata.so /stock/
COPY --from=stock /system/lib64/libbinder_ndk.so /stock/
COPY --from=stock /system/lib64/libnativewindow.so /stock/
COPY --from=stock /system/lib64/libcutils.so /stock/
COPY src /camera-hal/src
COPY build.sh /camera-hal/
RUN mkdir /out && /camera-hal/build.sh /out "${TARGETARCH}"

FROM stock
COPY --from=stock /vendor/lib64/hw/gralloc.redroid.so /vendor/lib64/hw/gralloc.redroid-real.so
COPY --from=build /out/gralloc.redroid.so /vendor/lib64/hw/gralloc.redroid.so
COPY --from=build /out/camera-provider /vendor/bin/hw/android.hardware.camera.provider-service.redroid
COPY etc/redroid-camera.rc /vendor/etc/init/redroid-camera.rc
COPY etc/redroid-camera.xml /vendor/etc/vintf/manifest/redroid-camera.xml
COPY etc/android.hardware.camera.xml /vendor/etc/permissions/android.hardware.camera.xml
