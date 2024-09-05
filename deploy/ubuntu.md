# Deploy redroid on ubuntu
```
##############################
## Ubuntu 22.04
##############################

## install required kernel modules
apt install linux-modules-extra-`uname -r`
modprobe binder_linux devices="binder,hwbinder,vndbinder"
### optional module (removed since 5.18)
modprobe ashmem_linux

## running redroid
docker run -itd --rm --privileged \
    --pull always \
    -v ~/data11:/data \
    -p 5555:5555 \
    --name redroid11 \
    redroid/redroid:12.0.0_64only-latest


##############################
## Ubuntu 20.04
##############################

## install required kernel modules
apt install linux-modules-extra-`uname -r`
modprobe binder_linux devices="binder,hwbinder,vndbinder"
### optional module (removed since 5.18)
modprobe ashmem_linux

## running redroid normally


##############################
## Ubuntu 18.04
##############################

## upgrade kernel (5.0+)
apt install linux-generic-hwe-18.04

## add possible missing kernel module
modprobe nfnetlink

## install required kernel modules
modprobe binder_linux devices="binder,hwbinder,vndbinder"
### optional module (removed since 5.18)
modprobe ashmem_linux

## running redroid normally
```
