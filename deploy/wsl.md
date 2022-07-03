# Deploy redroid on WSL2

```
## use custom kernel here

## download WSL kernel source from https://github.com/microsoft/WSL2-Linux-Kernel/tags
wget https://github.com/microsoft/WSL2-Linux-Kernel/archive/refs/tags/linux-msft-wsl-5.10.102.1.tar.gz

tar xf linux-msft-wsl-5.10.102.1.tar.gz
cd WSL2-Linux-Kernel-linux-msft-wsl-5.10.102.1

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


## build kernel
make -j`nproc`

## copy `arch/x86_64/boot/bzImage` to some folder in Windows

## follow https://docs.microsoft.com/en-us/windows/wsl/wsl-config#configuration-setting-for-wslconfig
## to use this new kernel
cat .wslconfig
[wsl2]
kernel=D:\\wsl\\bzImage
memory=4GB


## running redroid
docker run -itd --rm --privileged \
    --pull always \
    -v ~/data11:/data \
    -p 5555:5555 \
    --name redroid11 \
    redroid/redroid:11.0.0-latest
```
