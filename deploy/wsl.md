# Deploy redroid on WSL2

```
##############################
## 5.15
## 5.10
##############################

## download WSL kernel source from https://github.com/microsoft/WSL2-Linux-Kernel/tags

tar xf tar xf linux-msft-wsl-*.tar.gz
cd WSL2-Linux-Kernel-linux-msft-wsl-*
cp Microsoft/config-wsl .config

## enable following kernel features

# ipv6 should enabled
CONFIG_IPV6_ROUTER_PREF=y
CONFIG_IPV6_ROUTE_INFO=y
CONFIG_IPV6_MULTIPLE_TABLES=y
CONFIG_IPV6_SUBTREES=y

# codec2 required, can use ION for legacy kernel
CONFIG_DMABUF_HEAPS=y
CONFIG_DMABUF_HEAPS_SYSTEM=y

# optional, can use memfd
CONFIG_STAGING=y
CONFIG_ASHMEM=y

# binderfs required
CONFIG_ANDROID=y
CONFIG_ANDROID_BINDER_IPC=y
CONFIG_ANDROID_BINDERFS=y
CONFIG_ANDROID_BINDER_DEVICES="binder,hwbinder,vndbinder"

## install required libraries to build kernel
sudo apt install make gcc flex bison libssl-dev libelf-dev -y

## build kernel
make

## built kernel located in `arch/x86_64/boot/bzImage`

## follow https://docs.microsoft.com/en-us/windows/wsl/wsl-config#configuration-setting-for-wslconfig
## to use this new kernel
<<EOF cat >> .wslconfig
[wsl2]
kernel=<KERNEL-PATH>
EOF


## running redroid
docker run -d --rm \
    --privileged \
    -v ~/redroid-data:/data \
    -p 5555:5555 \
    --name redroid \
    redroid/redroid
```
