# Deploy redroid on Amazon Linux
*5.4* / *4.14* kernel supported currently (*5.10* support still under construction).

Check [redroid-modules](https://github.com/remote-android/redroid-modules) to install
required kernel modules.

```
## running redroid
docker run -itd --rm --privileged \
    --pull always \
    -v ~/data11:/data \
    -p 5555:5555 \
    --name redroid11 \
    redroid/redroid:11.0.0-latest
```
