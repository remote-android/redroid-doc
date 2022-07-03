# Deploy redroid on ubuntu
```
# for 5.0+ kernel

## Android required kernel features are packaged in `linux-modules-extra-xxx`
apt install linux-modules-extra-`uname -r`
modprobe binder_linux devices="binder,hwbinder,vndbinder"
modprobe ashmem_linux

## auto load modules
cat /etc/modules-load.d/redroid.conf
binder_linux
ashmem_linux

cat /etc/modprobe.d/redroid.conf
options binder_linux devices="binder,hwbinder,vndbinder"


# for kernel version before 5.0

## option A:
### upgrade to 5.0+ kernel
### https://wiki.ubuntu.com/Kernel/LTSEnablementStack

## option B:
### check https://github.com/remote-android/redroid-modules
### to install out of tree `binderfs` / `ashmem` kernel modules


# running redroid
docker run -itd --rm --privileged \
    --pull always \
    -v ~/data11:/data \
    -p 5555:5555 \
    --name redroid11 \
    redroid/redroid:11.0.0-latest

```
