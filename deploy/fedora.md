# Deploy redroid on Fedora Linux

```
## update kernel (binderfs seems introduce kernel panic on 5.17)
dnf update
reboot

## disable SELinux temporarily
setenforce 0

## run with memfd enabled
docker run -itd --rm --privileged \
    --pull always \
    -v ~/data11:/data \
    -p 5555:5555 \
    --name redroid11 \
    redroid/redroid:11.0.0-latest \
    androidboot.use_memfd=1
```
