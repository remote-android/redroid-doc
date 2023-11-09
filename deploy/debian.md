# Deploy redroid on Debian

```
##############################
## Debian 12
## Debian 11
##############################

### add binder devices
modprobe binder_linux devices=binder1,binder2,binder3,binder4,binder5,binder6
chmod 666 /dev/binder*

### start redroid
docker run -itd --rm --privileged \
    --pull always \
    -v /dev/binder1:/dev/binder \
    -v /dev/binder2:/dev/hwbinder \
    -v /dev/binder3:/dev/vndbinder \
    -v ~/data11:/data \
    -p 5555:5555 \
    --name redroid11 \
    redroid/redroid:11.0.0-latest


### NOTE:
### try enable binderfs in kernel to run unlimited redroid containers
### no need to mount binder devices if binderfs enabled

CONFIG_ANDROID_BINDER_IPC=y
CONFIG_ANDROID_BINDERFS=y
CONFIG_ANDROID_BINDER_DEVICES="binder,hwbinder,vndbinder"
```
