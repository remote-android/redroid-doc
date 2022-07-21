# Deploy redroid on OpenEuler

```
## use custom kernel 5.10.* LTS

# codec2 required
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


## run with memfd enabled
docker run -itd --rm --privileged \
    --pull always \
    -v ~/data11:/data \
    -p 5555:5555 \
    --name redroid11 \
    redroid/redroid:11.0.0-latest \
    androidboot.use_memfd=1
```
