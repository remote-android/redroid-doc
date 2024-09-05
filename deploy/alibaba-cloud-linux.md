# Deploy on Alibaba Cloud Linux

Check [redroid-modules](https://github.com/remote-android/redroid-modules) to install
required kernel modules.

```
## running redroid
docker run -itd --rm --privileged \
    --pull always \
    -v ~/data11:/data \
    -p 5555:5555 \
    --name redroid11 \
    redroid/redroid:12.0.0_64only-latest
```
