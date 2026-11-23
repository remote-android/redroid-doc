#!/bin/bash
# Compiles the camera provider and the gralloc wrapper with the NDK, without an
# AOSP tree. Runs in the Dockerfile's build stage; /stock holds the stock image's
# libraries.
#   build.sh <out-dir> <amd64|arm64>
set -euo pipefail
cd "$(dirname "$0")"
OUT="$1"
STOCK=/stock
NDK=/ndk/toolchains/llvm/prebuilt/linux-x86_64
RT="$(ls -d "$NDK"/lib/clang/*)"
case "$2" in
  arm64) TRIPLE=aarch64-linux-android; RTARCH=aarch64 ;;
  amd64) TRIPLE=x86_64-linux-android; RTARCH=x86_64 ;;
  *) echo "Unsupported architecture" >&2; exit 1 ;;
esac
export PATH="$NDK/bin:$PATH"
TARGET="--target=${TRIPLE}34 --sysroot=$NDK/sysroot"
HI=/src/interfaces
SM=/src/headers
GEN=/tmp/gen
# name, directory in hardware/interfaces, frozen version used by Android 14.
INTERFACES="android.hardware.camera.common camera/common/aidl 1
android.hardware.camera.metadata camera/metadata/aidl 2
android.hardware.camera.device camera/device/aidl 2
android.hardware.camera.provider camera/provider/aidl 2
android.hardware.graphics.common graphics/common/aidl 4
android.hardware.common common/aidl 2
android.hardware.common.fmq common/fmq/aidl 1"
INC=()
while read -r name dir ver; do INC+=("-I$HI/$dir/aidl_api/$name/$ver"); done <<< "$INTERFACES"
while read -r name dir ver; do
  api="$HI/$dir/aidl_api/$name/$ver"
  aidl --lang=ndk --structured --stability=vintf --min_sdk_version=current \
    --version="$ver" --hash="$(tail -1 "$api/.hash")" "${INC[@]}" \
    -o "$GEN/src" -h "$GEN/inc" $(find "$api" -name '*.aidl')
done <<< "$INTERFACES"

CXX="clang++ $TARGET -std=c++20 -O2 -fPIE -w -I$GEN/inc -I$SM -Isrc -I/src/stb -D__ANDROID_VNDK__"
mkdir -p /tmp/obj
find "$GEN/src" -name '*.cpp' -print0 |
  xargs -0 -P "$(nproc)" -I{} sh -c "$CXX -c '{}' -o /tmp/obj/\$(echo '{}' | tr / _).o"
$CXX -c src/main.cpp -o /tmp/obj/main.o
LINK="-fuse-ld=lld --rtlib=compiler-rt -L$RT/lib/linux/$RTARCH -L$STOCK"
$CXX -pie $LINK --unwindlib=libunwind -static-libstdc++ -Wl,--allow-shlib-undefined \
  -o "$OUT/camera-provider" /tmp/obj/*.o -lcamera_metadata -lbinder_ndk \
  -lnativewindow -lcutils -llog -lc++_static -lc++abi
clang $TARGET -O2 -fPIC -shared -w -fvisibility=hidden $LINK --unwindlib=none -I$SM \
  src/gralloc_wrap.c -Wl,-soname,gralloc.redroid.so -llog -ldl -o "$OUT/gralloc.redroid.so"
