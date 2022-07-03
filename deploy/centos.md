# Deploy redroid on CentOS
```
## use custom 5.10 kernel

### enable kernel features for x86_64

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


### extra kernel features for arm64

CONFIG_COMPAT=y
CONFIG_COMPAT_32BIT_TIME=y

CONFIG_ARM64_4K_PAGES=y


## disable SELinux temporarily
setenforce 0

## running redroid
docker run -itd --rm --privileged \
    --pull always \
    -v ~/data11:/data \
    -p 5555:5555 \
    --name redroid11 \
    redroid/redroid:11.0.0-latest
```
