# Deploy redroid 

*redroid* should be able to run on any Linux environment as long as the 
required kernel features are available (`binderfs`, etc.). Fortunately, these 
kernel features are already enabled in some Linux distros
(extra packages might needed). *redroid* also provides
[redroid-modules](https://github.com/remote-android/redroid-modules) repo
to support those distros without the required kernel features. And as last
resort, it's always capable via customizing Linux kernel.


**mandatory kernel features**
- `binderfs`
- `ashmem` / `memfd`
- `IPv6`
- `ION` / `DMA-BUF Heaps`
- 4KB page size
- ...


**deployment per distro / platform**
- [Alibaba-Cloud-Linux](alibaba-cloud-linux.md)
- [Amazon-Linux](amazon-linux.md)
- [Arch-Linux](arch-linux.md)
- [CentOS](centos.md)
- [Debian](debian.md)
- [Deepin](deepin.md)
- [Fedora](fedora.md)
- [Gentoo](gentoo.md)
- [Kubernetes](kubernetes.md)
- [LXC](lxc.md)
- [Mint](mint.md)
- [OpenEuler](openeuler.md)
- [openKylin](openkylin.md)
- [PopOS](pop_os.md)
- [Ubuntu](ubuntu.md)
- [WSL](wsl.md)


**general deploy redroid instructions**
```
## install docker https://docs.docker.com/engine/install/#server
## or use podman

## make sure required kernel features enabled; Check details on per distro page

## disable SELinux if present
setenforce 0

## running redroid
docker run -itd --rm --privileged \
    --pull always \
    -v ~/data11:/data \
    -p 5555:5555 \
    --name redroid11 \
    redroid/redroid:12.0.0_64only-latest

### Explanation:
###   --pull always    -- use latest image
###   -v ~/data11:/data  -- mount data partition
###   -p 5555:5555     -- expose adb port


## install adb https://developer.android.com/studio#downloads
adb connect localhost:5555
### NOTE: change localhost to IP if running redroid remotely

## view redroid screen
## install scrcpy https://github.com/Genymobile/scrcpy/blob/master/README.md#get-the-app
scrcpy -s localhost:5555
### NOTE: change localhost to IP if running redroid remotely
###     typically running scrcpy on your local PC


## running 64bit-only redroid
## only `aarch64` supported by some Arm platforms, the 64only image required here
docker run -itd --rm --privileged \
    --pull always \
    -v ~/data12_64only:/data \
    -p 5555:5555 \
    --name redroid12_64only \
    redroid/redroid:12.0.0_64only-latest \
```
