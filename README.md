English | [简体中文](README.zh-cn.md)

# Table of contents
- [Overview](#overview)
- [Getting Started](#getting-started)
- [Configuration](#configuration)
- [Native Bridge Support](#native-bridge-support)
- [GMS Support](#gms-support)
- [WebRTC Streaming](#webrtc-streaming)
- [How To Build](#how-to-build)
- [Troubleshooting](#troubleshooting)
- [Contact Me](#contact-me)
- [License](#license)

## Overview
*redroid* (*Re*mote an*Droid*) is a GPU accelerated AIC (Android In Cloud) solution. You can boot many
instances in Linux host (`Docker`, `podman`, `k8s` etc.). *redroid* supports both `arm64` and `amd64` architectures. 
*redroid* is suitable for Cloud Gaming, Virtualise Phones, Automation Test and more.

![Screenshot of redroid 11](./assets/redroid11.png)

Currently supported:
- Android 14 (`redroid/redroid:14.0.0-latest`)
- Android 14 64bit only (`redroid/redroid:14.0.0_64only-latest`)
- Android 13 (`redroid/redroid:13.0.0-latest`)
- Android 13 64bit only (`redroid/redroid:13.0.0_64only-latest`)
- Android 12 (`redroid/redroid:12.0.0-latest`)
- Android 12 64bit only (`redroid/redroid:12.0.0_64only-latest`)
- Android 11 (`redroid/redroid:11.0.0-latest`)
- Android 10 (`redroid/redroid:10.0.0-latest`)
- Android 9 (`redroid/redroid:9.0.0-latest`)
- Android 8.1 (`redroid/redroid:8.1.0-latest`)


## Getting Started
*redroid* should capabale running on any linux (with some kernel features enabled).

Quick start on *Ubuntu 20.04* here; Check [deploy section](deploy/README.md) for other distros.

```bash
## install docker https://docs.docker.com/engine/install/#server

## install required kernel modules
apt install linux-modules-extra-`uname -r`
modprobe binder_linux devices="binder,hwbinder,vndbinder"
modprobe ashmem_linux


## running redroid
docker run -itd --rm --privileged \
    --pull always \
    -v ~/data:/data \
    -p 5555:5555 \
    redroid/redroid:11.0.0-latest

### Explanation:
###   --pull always    -- use latest image
###   -v ~/data:/data  -- mount data partition
###   -p 5555:5555     -- expose adb port

### DISCLAIMER
### Should NOT expose adb port on public network
### otherwise, redroid container (even host OS) may get compromised

## install adb https://developer.android.com/studio#downloads
adb connect localhost:5555
### NOTE: change localhost to IP if running redroid remotely

## view redroid screen
## install scrcpy https://github.com/Genymobile/scrcpy/blob/master/README.md#get-the-app
scrcpy -s localhost:5555
### NOTE: change localhost to IP if running redroid remotely
###     typically running scrcpy on your local PC
```

## Configuration

```
## running redroid with custom settings (custom display for example)
docker run -itd --rm --privileged \
    --pull always \
    -v ~/data:/data \
    -p 5555:5555 \
    redroid/redroid:11.0.0-latest \
    androidboot.redroid_width=1080 \
    androidboot.redroid_height=1920 \
    androidboot.redroid_dpi=480 \
```

| Param | Description | Default |
| --- | --- | --- |
| `androidboot.redroid_width` | display width | 720 |
| `androidboot.redroid_height` | display height | 1280 |
| `androidboot.redroid_fps` | display FPS | 30(GPU enabled)<br> 15 (GPU not enabled)|
| `androidboot.redroid_dpi` | display DPI | 320 |
| `androidboot.use_memfd` | use `memfd` to replace deprecated `ashmem`<br>plan to enable by default | false |
| `androidboot.use_redroid_overlayfs` | use `overlayfs` to share `data` partition<br>`/data-base`: shared `data` partition<br>`/data-diff`: private data | 0 |
| `androidboot.redroid_net_ndns` | number of DNS server, `8.8.8.8` will be used if no DNS server specified | 0 |
| `androidboot.redroid_net_dns<1..N>` | DNS | |
| `androidboot.redroid_net_proxy_type` | Proxy type; choose from: `static`, `pac`, `none`, `unassigned` | |
| `androidboot.redroid_net_proxy_host` | | |
| `androidboot.redroid_net_proxy_port` | | 3128 |
| `androidboot.redroid_net_proxy_exclude_list` | comma seperated list | |
| `androidboot.redroid_net_proxy_pac` | | |
| `androidboot.redroid_gpu_mode` | choose from: `auto`, `host`, `guest`;<br>`guest`: use software rendering;<br>`host`: use GPU accelerated rendering;<br>`auto`: auto detect | `auto` |
| `androidboot.redroid_gpu_node` | | auto-detect |
| `ro.xxx`| **DEBUG** purpose, allow override `ro.xxx` prop; For example, set `ro.secure=0`, then root adb shell provided by default | |


## Native Bridge Support
It's possible to run `arm` Apps in `x86` *redroid* instance via `libhoudini`, `libndk_translation` or `QEMU translator`.

Check [@zhouziyang/libndk_translation](https://github.com/zhouziyang/libndk_translation) for prebuilt `libndk_translation`.
Published `redroid` images already got `libndk_translation` included.

``` bash
# example structure, be careful the file owner and mode

system/
├── bin
│   ├── arm
│   └── arm64
├── etc
│   ├── binfmt_misc
│   └── init
├── lib
│   ├── arm
│   └── libnb.so
└── lib64
    ├── arm64
    └── libnb.so
```

```dockerfile
# Dockerfile
FROM redroid/redroid:11.0.0-latest

ADD native-bridge.tar /
```

```bash
# build docker image
docker build . -t redroid:11.0.0-nb

# running
docker run -itd --rm --privileged \
    -v ~/data11-nb:/data \
    -p 5555:5555 \
    redroid:11.0.0-nb \
```

## GMS Support

It's possible to add GMS (Google Mobile Service) support in *redroid* via [Open GApps](https://opengapps.org/), [MicroG](https://microg.org/) or [MindTheGapps](https://gitlab.com/MindTheGapps/vendor_gapps).

Check [android-builder-docker](./android-builder-docker) for details.


## WebRTC Streaming
Plan to port `WebRTC` solutions from `cuttlefish`, including frontend (HTML5), backend and many virtual HALs.

## How To Build
It's Same as AOSP building process. But I suggest to use `docker` to build.

Check [android-builder-docker](./android-builder-docker) for details.

## Troubleshooting
- How to collect debug blobs
> `curl -fsSL https://raw.githubusercontent.com/remote-android/redroid-doc/master/debug.sh | sudo bash -s -- [CONTAINER]`
>
> omit *CONTAINER* if not exist any more

- Container disappeared immediately
> make sure the required kernel modules are installed; run `dmesg -T` for detailed logs

- Container running, but adb cannot connect (device offline etc.)
> run `docker exec -it <container> sh`, then check `ps -A` and `logcat`
>
> try `dmesg -T` if cannot get a container shell

## Contact Me
- remote-android.slack.com (invite link: https://join.slack.com/t/remote-android/shared_invite/zt-q40byk2o-YHUgWXmNIUC1nweQj0L9gA)
- ziyang.zhou@outlook.com

## License
*redroid* itself is under [Apache License](https://www.apache.org/licenses/LICENSE-2.0), since *redroid* includes 
many 3rd party modules, you may need to examine license carefully.

*redroid* kernel modules are under [GPL v2](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
