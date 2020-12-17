# Build ReDroid

## Get Source
- `repo init -u git@github.com:remote-android/platform_manifests.git -b [BRANCH] && repo sync -c`
BRANCH could be redroid-8.1.0, redroid-9.0.0, redroid-10.0.0, redroid-11.0.0 ...

## Build
suggest build with [docker](./docker)

```bash
# intent changes for redroid-10.0.0 (copyfile hook during repo sync), DO NOT Panic
# [Android Clang/LLVM Toolchain](https://android.googlesource.com/toolchain/llvm_android/)

project prebuilts/clang/host/linux-x86/         (*** NO BRANCH ***)
 -m     clang-r353983c/lib64/clang/9.0.3/lib/linux/libclang_rt.scudo_minimal-aarch64-android.a
 -m     clang-r353983c/lib64/clang/9.0.3/lib/linux/libclang_rt.scudo_minimal-arm-android.a
 -m     clang-r353983c/lib64/clang/9.0.3/lib/linux/libclang_rt.scudo_minimal-i686-android.a
 -m     clang-r353983c/lib64/clang/9.0.3/lib/linux/libclang_rt.scudo_minimal-x86_64-android.a
```

## Run
1. `export BUILD_OUT=~/redroid_out`
2. create rootfs only image (Dev purpose): `cd $BUILD_OUT && sudo tar --xattrs -c -C root . | docker import -c 'ENTRYPOINT ["/init", "qemu=1", "androidboot.hardware=redroid"]' - redroid-dev`
3. run with rootfs only image: `docker run -v ~/data:/data -itd -p 5900:5900 -p 5555:5555 --rm --memory-swappiness=0 --privileged -v $BUILD_OUT/redroid_x86_64/system:/system -v $BUILD_OUT/redroid_x86_64/vendor:/vendor redroid-dev`
4. create full image: `cd $BUILD_OUT && sudo tar --xattrs -c system vendor -C root --exclude="system" --exclude="vendor" . | docker import -c 'ENTRYPOINT ["/init", "qemu=1", "androidboot.hardware=redroid"]' - redroid`
5. run with full image: `docker run -v ~/data:/data -itd -p 5900:5900 -p 5555:5555 --rm --memory-swappiness=0 --privileged redroid`
