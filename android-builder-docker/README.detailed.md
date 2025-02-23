# Build Redroid with Docker
```
WARNING: YOU NEED ~200GB OF FREE SPACE ON HDD TO COMPLETE THE WHOLE PROCESS (~250GB IF BUILDING WITH GAPPS)
```
#### 1) Setup
```
# update and install packages
sudo apt-get update -y && sudo apt-get upgrade -y && sudo apt-get install -y curl libxml2-utils docker.io docker-buildx git-lfs jq
sudo apt-get remove repo -y && sudo apt-get autoremove -y

# install 'repo'
mkdir -p ~/.bin
curl https://storage.googleapis.com/git-repo-downloads/repo > ~/.bin/repo
chmod a+x ~/.bin/repo
echo 'export PATH=~/.bin:$PATH' >> ~/.bashrc && source ~/.bashrc

# set git variables
git config --global user.email "you@example.com"
git config --global user.name "Your Name"

# add current user to Docker group so we don't have to type 'sudo' to run Docker
sudo usermod -aG docker ubuntu # Set your username here
sudo systemctl enable docker
sudo systemctl restart docker

# NOW YOU SHOULD LOGOUT AND LOGIN AGAIN FOR DOCKER TO RECOGNIZE OUR USER

# check if Docker is running
docker --version && docker ps -as
# Docker version 24.0.5, build 24.0.5-0ubuntu1~22.04.1
# CONTAINER ID   IMAGE     COMMAND   CREATED   STATUS    PORTS     NAMES     SIZE
```
#### 2) Fetch and sync Android and Redroid codes
```
mkdir ~/redroid && cd ~/redroid

# check supported branch in https://github.com/remote-android/redroid-patches.git
repo init -u https://android.googlesource.com/platform/manifest --git-lfs --depth=1 -b android-12.0.0_r34

# add local manifests
git clone https://github.com/remote-android/local_manifests.git ~/redroid/.repo/local_manifests -b 12.0.0

# sync code | ~100GB of data | ~20 minutes on a fast CPU + connection
repo sync -c -j$(nproc)

# get latest Dockerfile from Redroid repository
wget https://raw.githubusercontent.com/remote-android/redroid-doc/master/android-builder-docker/Dockerfile

# check if 'Webview.apk' files were properly synced by 'git-lfs'. Each .apk should be at least ~80MB in size.
find ~/redroid/external/chromium-webview -type f -name "*.apk" -exec du -h {} +
```
#### 3) GApps (optional)
##### In case you want to add GApps to your build (PlayStore, etc), you can follow these steps, otherwise, just skip it
- Add this manifest under `.repo/local_manifests/mindthegapps.xml`, for the specific redroid revision selected.
  For example, for Redroid 11 the revision is 'rho', and for Redroid 12 is 'sigma', and this is the expected manifest:
  ```xml
  <?xml version="1.0" encoding="UTF-8"?>
  <manifest>
    <remote name="mtg" fetch="https://gitlab.com/MindTheGapps/" />
    <project path="vendor/gapps" name="vendor_gapps" revision="sigma" remote="mtg" />
  </manifest>
  ```
  You can add manually **↑** or with this 1 line command **↓** that will create and write to the file (remember to set proper revision)
  ```
  echo -e '<?xml version="1.0" encoding="UTF-8"?>\n<manifest>\n  <remote name="mtg" fetch="https://gitlab.com/MindTheGapps/" />\n  <project path="vendor/gapps" name="vendor_gapps" revision="sigma" remote="mtg" />\n</manifest>' > ~/redroid/.repo/local_manifests/mindthegapps.xml
  ```
- Add the path to the mk file corresponding to your selected arch `~/redroid/device/redroid/redroid_ARCHITECTURE/device.mk`, for example if we want arm64 arch (arm64 for Redroid 12 as in 'sigma' Mind The Gapps, as only arm64 GApps)
  ```
  nano ~/redroid/device/redroid/redroid_arm64/device.mk
  ```
  ```makefile
  $(call inherit-product, vendor/gapps/arm64/arm64-vendor.mk)
  ```
- Resync `~/redroid` | Fetch GApps code
  ```
  repo sync -c -j$(nproc)
  ```
- Patch GApps files
  ```  
  # First we comment out some lines in "~/redroid/vendor/gapps/arm64/arm64-vendor.mk"
  sed -i '/^PRODUCT_COPY_FILES += /s/^/#/' ~/redroid/vendor/gapps/arm64/arm64-vendor.mk
  sed -i '/vendor\/gapps\/arm64\/proprietary\/product/s/^/#/' ~/redroid/vendor/gapps/arm64/arm64-vendor.mk

  # Then we create the "~/redroid/vendor/gapps/arm64/proprietary/product/app/MarkupGoogle/Android.bp"
  echo 'cc_prebuilt_library_shared {
      name: "libsketchology_native",
      srcs: ["lib/arm64/libsketchology_native.so"],
      shared_libs: [],
      stl: "none",
      system_shared_libs: [],
      vendor: true,
  }' > ~/redroid/vendor/gapps/arm64/proprietary/product/app/MarkupGoogle/Android.bp

  # And add some entries to the end of "~/redroid/vendor/gapps/arm64/Android.bp" file
  sed -i '$a cc_prebuilt_library_shared {\n    name: "libjni_latinimegoogle",\n    srcs: ["proprietary/product/lib/libjni_latinimegoogle.so"],\n    shared_libs: [],\n    stl: "none",\n    system_shared_libs: [],\n    vendor: true,\n}\n\ncc_prebuilt_library_shared {\n    name: "libjni_latinimegoogle_64",\n    srcs: ["proprietary/product/lib64/libjni_latinimegoogle.so"],\n    shared_libs: [],\n    stl: "none",\n    system_shared_libs: [],\n    vendor: true,\n}' ~/redroid/vendor/gapps/arm64/Android.bp
  ```
  
- OPTIONAL (recommended)
  
  While importing the image in the Step 6 (docker import command), change the entrypoint to `ENTRYPOINT ["/init", "androidboot.hardware=redroid", "ro.setupwizard.mode=DISABLED"]`, so you avoid doing it manually at every container start, or if you want set `ro.setupwizard.mode=DISABLED` at container start, skipping the GApps setup wizard at redroid boot. Optional line available in Step 6.

#### 4) Apply Redroid patches, create builder and start it
```
# apply redroid patches
git clone https://github.com/remote-android/redroid-patches.git ~/redroid-patches
~/redroid-patches/apply-patch.sh ~/redroid

docker buildx create --use
docker buildx build --build-arg userid=$(id -u) --build-arg groupid=$(id -g) --build-arg username=$(id -un) -t redroid-builder --load .
docker run -it --privileged --rm --hostname redroid-builder --name redroid-builder -v ~/redroid:/src redroid-builder
```
#### 5) Build Redroid
#### Now we should be in the `redroid-builder` docker container
```
cd /src
. build/envsetup.sh

lunch redroid_arm64-userdebug
# redroid_x86_64-userdebug
# redroid_arm64-userdebug
# redroid_x86_64_only-userdebug (64 bit only, redroid 12+)
# redroid_arm64_only-userdebug (64 bit only, redroid 12+)

# start to build | + ~100GB of data | ~ 2 hours on a fast cpu
m -j$(nproc)

exit
```
#### 6) Create Redroid image in *HOST*
```
cd ~/redroid/out/target/product/redroid_arm64
sudo mount system.img system -o ro
sudo mount vendor.img vendor -o ro

# set the target platform(s) with the '--platform flag' below. eg: --platform=linux/arm64,linux/amd64 ....
sudo tar --xattrs -c vendor -C system --exclude="./vendor" . | docker import --platform=linux/arm64 -c 'ENTRYPOINT ["/init", "qemu=1", "androidboot.hardware=redroid"]' - redroid
# execute only ↑ (without Step 3 Gapps) or ↓ (with Step 3 Gapps) | (OPTIONAL)
sudo tar --xattrs -c vendor -C system --exclude="./vendor" . | docker import --platform=linux/arm64 -c 'ENTRYPOINT ["/init", "qemu=1", "androidboot.hardware=redroid", "ro.setupwizard.mode=DISABLED"]' - redroid

sudo umount system vendor

# create rootfs only image for develop purpose (Optional)
tar --xattrs -c -C root . | docker import -c 'ENTRYPOINT ["/init", "androidboot.hardware=redroid"]' - redroid-dev

# inspect the created image to see if everything is ok
docker inspect redroid:latest | jq '.[0].Config.Entrypoint, .[0].Architecture'
[
  "/init",
  "qemu=1",
  "androidboot.hardware=redroid"
]
"arm64"
```
#### 7) Tag and push image to Docker Hub (optional)
##### Now you can tag the docker image to your personal docker account and push it to Docker Hub
```
# Get the container ID of the imported Docker image
docker images
REPOSITORY             TAG                              IMAGE ID       CREATED         SIZE
redroid                latest                           b00684099b2d   9 minutes ago   1.91GB

# Tag it
docker tag b00684099b2d YourDockerAccount/redroid:12.0.0_arm64-latest

# Push it to Docker Hub
docker login
docker push YourDockerAccount/redroid:12.0.0_arm64-latest
```
#### 8) Run the container
```
docker run -itd --privileged --name Redroid12 -v ~/data:/data -p 5555:5555 YourDockerAccount/redroid:12.0.0_arm64-latest
# or just use the default tag if you skipped Step 7
docker run -itd --privileged --name Redroid12 -v ~/data:/data -p 5555:5555 redroid:latest
```
