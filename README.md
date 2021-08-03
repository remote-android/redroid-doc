English | [简体中文](zh/)

# ReDroid
**ReDroid** (*Re*mote an*Droid*) is a GPU accelerated AIC (Android In Container) solution. You can boot many
instances in Linux host (`Docker`, `K8S` etc.). 
*ReDroid* supports both arm64 and amd64 architectures. You can connect to *ReDroid* througth
`scrcpy` or `adb shell`. *ReDroid* is suitable for Cloud Gaming, VDI / VMI (Virtual Mobile Infurstrure), 
Automation Test and more.

Currently supported:
- Android 12 (`redroid/redroid:12.0.0-latest`, `redroid/redroid:12.0.0-amd64`, `redroid/redroid:12.0.0-arm64`)
- Android 11 (`redroid/redroid:11.0.0-latest`, `redroid/redroid:11.0.0-amd64`, `redroid/redroid:11.0.0-arm64`)
- Android 10 (`redroid/redroid:10.0.0-latest`, `redroid/redroid:10.0.0-amd64`, `redroid/redroid:10.0.0-arm64`)
- Android 9 (`redroid/redroid:9.0.0-latest`, `redroid/redroid:9.0.0-amd64`, `redroid/redroid:9.0.0-arm64`)
- Android 8.1 (`redroid/redroid:8.1.0-latest`, `redroid/redroid:8.1.0-amd64`, `redroid/redroid:8.1.0-arm64`)

**pull first, otherwise your running image may out of date; check image SHA**

Tested Platforms:
- Ubuntu 16.04 / 18.04 / 20.04 (amd64 / arm64)
- Amazon Linux 2 (amd64 / arm64)
- Alibaba Cloud Linux 2 (amd64)
- Alibaba Cloud Linux 3 (amd64 / arm64) with `podman-docker`
- WSL 2 (Ubuntu) (amd64)
- CentOS (amd64\*, arm64\*)
- OpenEuler 20.03 (amd64, arm64\*)

\* means need customized kernel

## Quick Start
*ReDroid* runs on modern linux (kernel 4.14+), and require some Android specific modules (`binderfs`, `ashmem` etc.)
check [kernel modules](https://github.com/remote-android/redroid-modules) to install these required kernel modules.

```bash
# start and connect via `scrcpy`
docker run -itd --rm --memory-swappiness=0 --privileged \
    --pull always \
    -v ~/data:/data \
    -p 5555:5555 \
    redroid/redroid:9.0.0-latest

adb connect <IP>:5555
scrcpy --serial <IP>:5555

## explains:
## --pull always  -- be sure to use the latest image
## -v ~/data:/data  -- mount data partition
## -p 5555:5555 -- 5555 for adb connect, you can run `adb connect <IP>`

```

## Start Params
required params (already added in docker image)
- qemu=1
- androidboot.hardware=redroid

display params
- redroid.width=720
- redroid.height=1280
- redroid.fps=15
- ro.sf.lcd_density=320
- redroid.enable_built_in_display=[0|1]
- redroid.overlayfs=[0|1]

GPU accelerating
*ReDroid* use mesa3d to accelerate 3D rendering.
Currently tested paltforms:
- AMD (arm64, amd64 with `amdgpu` driver)
- Intel (amd64 with `i915` driver)
- virtio-gpu (vendor agnostic, arm64 and amd64)

params:
- redroid.gpu.mode=[auto|host|guest]
- redroid.gpu.node=[/dev/dri/renderDxxx]
- qemu.gles.vendor=mesa
- ro.hardware.gralloc=gbm

Virtual WiFi (Experiment in ReDroid 10, *build broken, fix soon*)
- ro.kernel.qemu.wifi=1
Virtual WiFi is still under development, make sure `mac80211_hwsim` exist (`modprobe mac80211_hwsim`).
checkout `redroid-10-wifi` in `vendor/redroid` and `redroid-10.0.0` in `device/generic/goldfish` to make
your build. run `docker exec <container> ip r add default via 192.168.232.1 dev wlan0`

NOTE: you can override system props prefixed with `qemu.` or `ro.`. for example, you can set `ro.secure=0`, then 
you can get root adb shell by default.

## Deployment
*ReDroid* support different deploy methods, check [Deploy](./deploy.md) for more details.
- Docker (podman)
- K8S
- Package Manager (Planned)

## Native Bridge
It's possible to run Arm Apps in x64 *ReDroid* instance with `libhoudini`, `libndk_translator` or `Qemu translator`

Check [Native Bridge](./native_bridge) for more.

## GMS
It's possible to add GMS (Google Mobile Service) support in *ReDroid* via Google packages or `MicroG`.

Check [GMS](./gms.md) for more.

## WebRTC
Compared with `RFB` (VNC), `WebRTC` is more versatile (audio / video / camera / sensors data...). 
*ReDroid* is planning to implement `WebRTC` protocol. Check [WebRTC](./webrtc.md) for more.

## Build
Same as AOSP building process. check [AOSP setup](https://source.android.com/setup/build/initializing#installing-required-packages-ubuntu-1404)

Check [ReDroid build](./build.md) for more.

## Troubleshooting
- VNC screen hang
    - try `stop vncserver && start vncserver`
    - or reboot *ReDroid* instance `docker restart ...`
- icon in launcher gone
    - if switching between GPU / swiftshader (soft), try to clear launcher data or entire data partition
- `fuse` setup failed
    - try `modprobe fuse`

## Contributing
Contributing is always welcome (especially the `HAL` part). check [Contributing](./contributing.md) for more

## Workarounds
- Kernel 5.7+, need enable `binderfs` / `ashmem`
- `redroid` require `pid_max` less than 65535, or else may run into problems. Change in host OS, or add `pid_max` separation support in PID namespace
- SElinux is disabled in *ReDroid*; possible enabled with [selinuxns POC](http://namei.org/presentations/selinux_namespacing_lca2018.pdf)
- sdcardfs currently not implemented, use `fuse` instead; may need run `modprobe fuse` first in some OS (AmazonLinux2 ?)
- CGroups errors ignored; some (`stune` for example) not supported in generic linux.
- `procfs` not fully seperated with host OS; Community use `lxcfs` and some cloud vendor ([TencentOS](https://github.com/Tencent/TencentOS-kernel)) enhanced in their own kernel.
- vintf verify disabled (since no kernel)

## Contacts
- ziyang.zhou@outlook.com
- remote-android.slack.com (invite link: https://join.slack.com/t/remote-android/shared_invite/zt-q40byk2o-YHUgWXmNIUC1nweQj0L9gA)

## License
*ReDroid* itself is under [Apache License](https://www.apache.org/licenses/LICENSE-2.0), since *ReDroid* includes 
many 3rd party modules, you may need to examine license carefully.

*ReDroid* kernel modules are under [GPL v2](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)

