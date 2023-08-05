# Deploy redroid on Debian
```
##############################
## Debian 11 (kernel 5.18)
##############################

### the included `binder_linux` NOT support `binderfs`
### so use custom kernel here

### enable binderfs
CONFIG_ANDROID=y
CONFIG_ANDROID_BINDER_IPC=y
CONFIG_ANDROID_BINDERFS=y
CONFIG_ANDROID_BINDER_DEVICES="binder,hwbinder,vndbinder"


### running redroid
docker run -itd --rm --privileged \
    --pull always \
    -v ~/data11:/data \
    -p 5555:5555 \
    --name redroid11 \
    redroid/redroid:11.0.0-latest \


##############################
## Debian 12 (kernel 6.1)
##############################

### enable binderfs
CONFIG_ANDROID_BINDER_IPC=y
CONFIG_ANDROID_BINDERFS=y
CONFIG_ANDROID_BINDER_DEVICES="binder,hwbinder,vndbinder"

```
