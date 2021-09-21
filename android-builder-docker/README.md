# Build ReDroid with docker

## Sync Code
ReDroid manifest include several branches:
- `redroid12-gsi`, `redroid-s-beta-3`
- `redroid11-gsi`, `redroid-11.0.0`
- `redroid10-gsi`, `redroid-10.0.0`
- `redroid-9.0.0`, `redroid9-gsi` (experimental)
- `redroid-8.1.0`

branch with `-gsi` is recommended (fully treble / VNDK  enforced).

```bash
# fetch code

mkdir aosp && cd aosp
repo init -u https://github.com/remote-android/platform_manifests.git -b <BRANCH> --depth=1
repo sync -c --no-tags
```

## Build
```bash
# create builder docker image
# adjust apt.conf and source.list if needed
docker build --build-arg userid=$(id -u) --build-arg groupid=$(id -g) --build-arg username=$(id -un) -t android-build-trusty .

# *inside* builder container
cd /src

. build/envsetup.sh
lunch redroid_x86_64-userdebug
# or lunch redroid_arm64-userdebug
m

# create redroid docker image in *HOST*
cd <BUILD-OUT-DIR>
sudo mount system.img system -o ro
sudo mount vendor.img vendor -o ro
sudo tar --xattrs -c vendor -C system --exclude="vendor" . | docker import -c 'ENTRYPOINT ["/init", "qemu=1", "androidboot.hardware=redroid"]' - redroid
```

## Note
```bash
# intent changes for redroid-10 (copyfile hook during repo sync), DO NOT PANIC
# [Android Clang/LLVM Toolchain](https://github.com/remote-android/platform_manifests/tree/llvm-toolchain-redroid-10.0.0)

project prebuilts/clang/host/linux-x86/         (*** NO BRANCH ***)
 -m     clang-r353983c/lib64/clang/9.0.3/lib/linux/libclang_rt.scudo_minimal-aarch64-android.a
 -m     clang-r353983c/lib64/clang/9.0.3/lib/linux/libclang_rt.scudo_minimal-arm-android.a
 -m     clang-r353983c/lib64/clang/9.0.3/lib/linux/libclang_rt.scudo_minimal-i686-android.a
 -m     clang-r353983c/lib64/clang/9.0.3/lib/linux/libclang_rt.scudo_minimal-x86_64-android.a
```

