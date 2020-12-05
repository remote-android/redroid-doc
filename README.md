English | [简体中文](zh/)

# ReDroid
**ReDroid** (Remote Android) is a lightweight GPU accelerated Android Emulator. You can boot many 
instances in Linux host or any Linux container envrionments (`Docker`, `K8S`, `LXC` etc.). 
*ReDroid* supports both arm64 and x86_64 architectures. You can connect to *ReDroid* througth 
`VNC` or `WebRTC` (TODO) or `adb` shell. *ReDroid* is suitable for Cloud Gaming, 
VDI / VMI (Virtual Mobile Infurstrure), Automation Test and more.

Currently supported Android version:
- Android 8 (GPU enabled)
- Android 9 (GPU enabled)
- Android 10 (ongoing)

## Quick Started via Docker
*ReDroid* runs on modern linux (currently require kernel version 4.14+), and require some Android specific 
drivers (binder, ashmem at least) check [kernel modules](https://github.com/remote-android/redroid-modules) 
for more details.
```bash
# quick install kernel driver via docker, change *NODE_OS* to actual OS (ubuntu:18.04 for example)
docker run --cap-add CAP_SYS_MODULE <NODE_OS> --entrypoint bash <(curl -s https://raw.githubusercontent.com/remote-android/redroid-modules/master/deploy/build.sh)

# start a ReDroid instance via docker
docker run -v ~/data:/data -itd -p 5900:5900 -p 5555:5555 --rm --memory-swappiness=0 --privileged redroid/redroid

## explains:
## -v ~/data:/data  -- mount data partition
## -p 5900:5900 -- 5900 for VNC connect, you can connect via VncViewer with <IP>:5900
## -p 5555:5555 -- 5555 for adb connect, you can run `adb connect localhost`
```

## Start Params
required params (alrady added in image args)
- qemu=1
- androidboot.hardware=goldfish

display params
- redroid.width=720
- redroid.height=1280
- redroid.density=320
- redroid.fps=15

GPU accelerating
*ReDroid* use mesa3d to accelerate 3D rendering.
- qemu.gles.vendor=mesa
- ro.hardware.gralloc=[minigbm|minigbm_intel]

you can override system props prefixed with `qemu.` or `ro.`. for example, you can set `ro.secure=0`, then 
you can get root adb shell.

## Deployment
*ReDroid* support different deploy methods, check [Deploy](./deploy.md) for more details.
- Docker
- K8S
- virsh-lxc
- kata-runtime (microVM)
- Package Manager

## Native Bridge
It's possible to run Arm Apps in x86 *ReDroid* instance with `libhoudini`, `ARM translater` or `Qemu translater`
check [Native Bridge](./native_bridge.md) for more details.

## GMS
It's possible to add GMS (Google Mobile Service) support in *ReDroid* instance via Google packages or `MicroG`.
check [GMS](./gms.md) for more details.

## WebRTC
Compared with RFB (VNC), WebRTC is more versatile (audio / video / camera support...). 
*ReDroid* is planning to implement WebRTC protocol. Check [WebRTC](./webrtc.md) for more details.

## Workarounds
- SElinux is disabled in *ReDroid*; check [selinuxns POC](http://namei.org/presentations/selinux_namespacing_lca2018.pdf)
- sdcardfs currently not implemented as modules, use `fuse` instead;
- CGroups errors ignored; some cgroups path not same with generic linux, and some (`stune` for example) not supported
- `procfs` not fully seperated with host OS; community use `lxcfs` and some cloud vendor ([TencentOS](https://github.com/Tencent/TencentOS-kernel)) enhanced in their own kernel.
- vintf verify disabled (since no kernel)

