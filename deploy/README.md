# Deploy redroid 

*redroid* should capable to run on any linux environment as long as the 
required kernel features avaiable (`binderfs` etc.). Fortunately, these 
kernel features are already included in some linux distributions 
(maybe need seperate kernel / modules package). *redroid* also provide
[redroid-modules](https://github.com/remote-android/redroid-modules) repo
to support some linux distrutions without those kernel features. And as last
resort, custom kernels are always capable.


**madatory kernel features**
- `binderfs`
- `ashmem` / `memfd`
- `ipv6`
- `ion` / `dma_heap`
- 4KB page size
- ...


**deployment per distro / platform**
- [alibaba-cloud-linux](alibaba-cloud-linux.md)
- [amazon-linux](amazon-linux.md)
- [arch-linux](arch-linux.md)
- [centos](centos.md)
- [debian](debian.md)
- [fedora](fedora.md)
- [kubernetes](kubernetes.md)
- [Mint](mint.md)
- [ubuntu](ubuntu.md)
- [wsl](wsl.md)


**general deploy redroid instructions**
```
## install docker https://docs.docker.com/engine/install/#server

## make sure required kernel features enabled


## running redroid
docker run -itd --rm --privileged \
    --pull always \
    -v ~/data11:/data \
    -p 5555:5555 \
    --name redroid11 \
    redroid/redroid:11.0.0-latest

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


## running without `ashmem` (removed since linux 5.18)
## NOTE: plan to enable `memfd` by default
docker run -itd --rm --privileged \
    --pull always \
    -v ~/data11:/data \
    -p 5555:5555 \
    --name redroid11 \
    redroid/redroid:11.0.0-latest \
    androidboot.use_memfd=1


## running 64bit-only redroid (only redroid12 published)
## some ARM CPUs only support 64bit mode, or just want to remove legacy 32bit runtime
docker run -itd --rm --privileged \
    --pull always \
    -v ~/data12_64only:/data \
    -p 5555:5555 \
    --name redroid12_64only \
    redroid/redroid:12.0.0_64only-latest \

```
