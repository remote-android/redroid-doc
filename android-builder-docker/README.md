# Build redroid with docker

```bash
#####################
# fetch code
#####################
mkdir ~/redroid && cd ~/redroid

# check supported branch in https://github.com/remote-android/redroid-patches.git
repo init -u https://android.googlesource.com/platform/manifest --git-lfs --depth=1 -b 11.0.0_r48

# add local manifests
git clone https://github.com/remote-android/local_manifests.git ~/redroid/.repo/local_manifests -b 11.0.0

# sync code
repo sync -c

# apply redroid patches
git clone https://github.com/remote-android/redroid-patches.git ~/redroid-patches
~/redroid-patches/apply-patch.sh ~/redroid

#####################
# fetch code (LEGACY)
#####################
mkdir ~/redroid && cd ~/redroid

repo init -u https://github.com/remote-android/platform_manifests.git -b redroid-11.0.0 --depth=1 --git-lfs
# check @remote-android/platform_manifests for supported branch / manifest

repo sync -c

#####################
# create builder
#####################
docker build --build-arg userid=$(id -u) --build-arg groupid=$(id -g) --build-arg username=$(id -un) -t redroid-builder .

#####################
# start builder
#####################
docker run -it --rm --hostname redroid-builder --name redroid-builder -v ~/redroid:/src redroid-builder

#####################
# build redroid
#####################
cd /src

. build/envsetup.sh

lunch redroid_x86_64-userdebug
# redroid_arm64-userdebug
# redroid_x86_64_only-userdebug (64 bit only, redroid 12+)
# redroid_arm64_only-userdebug (64 bit only, redroid 12+)

# start to build
m

#####################
# create redroid image in *HOST*
#####################
cd ~/redroid/out/target/product/redroid_x86_64

sudo mount system.img system -o ro
sudo mount vendor.img vendor -o ro
sudo tar --xattrs -c vendor -C system --exclude="vendor" . | docker import -c 'ENTRYPOINT ["/init", "androidboot.hardware=redroid"]' - redroid
sudo umount system vendor

# create rootfs only image for develop purpose
tar --xattrs -c -C root . | docker import -c 'ENTRYPOINT ["/init", "androidboot.hardware=redroid"]' - redroid-dev
```

## Build with GApps

You can build a redroid image with your favorite GApps package if you need, for simplicity there is an example with Mind The Gapps.

This is not different from the normal building process, except for some small things, like:

- When following the "Sync Code" paragraph,  after running the repo sync, add this manifest under .repo/local_manifests/mindthegapps.xml, for the specific redroid revision selected.

  For example, for Redroid 11 the revision is 'rho', and for Redroid 12 is 'sigma', and this is the expected manifest:

  ```xml
  <?xml version="1.0" encoding="UTF-8"?>
  <manifest>
          <remote name="mtg" fetch="https://gitlab.com/MindTheGapps/" />
          <project path="vendor/gapps" name="vendor_gapps" revision="sigma" remote="mtg" />
  </manifest>
  ```

- Add the path to the mk file corresponding to your selected arch to `device/redroid/redroid_ARCHITECTURE/device.mk` , for example we want x86_64 arch (x86 for redroid 11 as in 'rho' Mind The Gapps as only x86 GApps)

  ```makefile
  $(call inherit-product, vendor/gapps/x86_64/x86_64-vendor.mk)
  ```

  putting this, modified for the corresponding architecture you need. So change 'x86_64' with arm64 if you need arm64 GApps.

  Resync the repo with a new `repo sync -c` and continue following the building guide exactly as before.

- OPTIONAL but recommended. While importing the image, change the entrypoint to 'ENTRYPOINT ["/init", "androidboot.hardware=redroid", "ro.setupwizard.mode=DISABLED"]' , so you avoid doing it manually at every container start, or if you want set `ro.setupwizard.mode=DISABLED` at container start, skipping the GApps setup wizard at redroid boot.
