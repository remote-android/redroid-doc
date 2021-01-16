English | [简体中文](zh/)

# ReDroid
**ReDroid** (Remote Android) is a lightweight GPU accelerated Android Emulator. You can boot many 
instances in Linux host or any Linux container envrionments (`Docker`, `K8S`, `LXC` etc.). 
*ReDroid* supports both arm64 and x86_64 architectures. You can connect to *ReDroid* througth 
`VNC` or `scrcpy` / `sndcpy` or `WebRTC` (TODO) or `adb shell`. *ReDroid* is suitable for Cloud Gaming, 
VDI / VMI (Virtual Mobile Infurstrure), Automation Test and more.

Currently supported:
- Android 11 (`redroid/redroid:11.0.0-latest`, `redroid/redroid:11.0.0-amd64`, `redroid/redroid:11.0.0-arm64`)
- Android 10 (`redroid/redroid:10.0.0-latest`, `redroid/redroid:10.0.0-amd64`, `redroid/redroid:10.0.0-arm64`)
- Android 9 (`redroid/redroid:9.0.0-latest`, `redroid/redroid:9.0.0-amd64`, `redroid/redroid:9.0.0-arm64`)
- Android 8.1 (`redroid/redroid:8.1.0-latest`, `redroid/redroid:8.1.0-amd64`, `redroid/redroid:8.1.0-arm64`)


## Quick Start
*ReDroid* runs on modern linux (kernel 4.14+), and require some Android specific modules (binder, ashmem at least) 
check [kernel modules](https://github.com/remote-android/redroid-modules) for more.

```bash
# install kernel modules
sudo bash -c "`curl -s https://raw.githubusercontent.com/remote-android/redroid-modules/master/deploy/build.sh`"

# start ReDroid instance and connect via VNC
docker run -v ~/data:/data -d -p 5900:5900 -p 5555:5555 --rm --memory-swappiness=0 --privileged redroid/redroid:10.0.0-latest redroid.vncserver=1

## explains:
## -v ~/data:/data  -- mount data partition
## -p 5900:5900 -- 5900 for VNC connect, you can connect via VncViewer with <IP>:5900
## -p 5555:5555 -- 5555 for adb connect, you can run `adb connect localhost`


# OR start ReDroid instance and connect via `scrcpy` (Performance boost, *recommended*)
docker run -v ~/data:/data -d -p 5555:5555 --rm --memory-swappiness=0 --privileged redroid/redroid:10.0.0-latest
adb connect <IP>:5555
scrcpy --serial <IP>:5555

```

## Start Params
required params (already added in image args)
- qemu=1
- androidboot.hardware=redroid

display params
- redroid.width=720
- redroid.height=1280
- redroid.fps=15
- ro.sf.lcd_density=320

VNC server
- redroid.vncserver=[0|1]

GPU accelerating
*ReDroid* use mesa3d to accelerate 3D rendering.
- qemu.gles.vendor=mesa
- ro.hardware.gralloc=minigbm

Virtual WiFi [Experiment in ReDroid 10]
- ro.kernel.qemu.wifi=1
Virtual WiFi is still under development, make sure `mac80211_hwsim` exist (`modprobe mac80211_hwsim`).
checkout `redroid-10-wifi` in `vendor/redroid` and `redroid-10.0.0` in `device/generic/goldfish` to make
your build. run `docker exec <container> ip r add default via 192.168.232.1 dev wlan0`

you can override system props prefixed with `qemu.` or `ro.`. for example, you can set `ro.secure=0`, then 
you can get root adb shell by default.

## Deployment
*ReDroid* support different deploy methods, check [Deploy](./deploy.md) for more details.
- Docker
- K8S
- virsh-lxc / lxd
- kata-runtime (microVM)
- Package Manager

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
- cannot connect to network (cannot `ping`)
    - `adb shell boot_completed.redroid.sh`
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
- SElinux is disabled in *ReDroid*; possible enabled with [selinuxns POC](http://namei.org/presentations/selinux_namespacing_lca2018.pdf)
- sdcardfs currently not implemented, use `fuse` instead; may need run `modprobe fuse` first in some OS (AmazonLinux2?)
- CGroups errors ignored; some cgroups path not same with generic linux, and some (`stune` for example) not supported
- `procfs` not fully seperated with host OS; community use `lxcfs` and some cloud vendor ([TencentOS](https://github.com/Tencent/TencentOS-kernel)) enhanced in their own kernel.
- vintf verify disabled (since no kernel)

## License
*ReDroid* itself is under [Apache License](https://www.apache.org/licenses/LICENSE-2.0), since *ReDroid* includes 
many 3rd party modules, you may need to examine license carefully.

*ReDroid* kernel modules are under [GPL v2](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)

