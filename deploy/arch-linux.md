# Deploy on Arch Linux

```
## install zen kernel
pacman -S linux-zen

## run with memfd enabled
docker run -itd --rm --privileged \
    --pull always \
    -v ~/data11:/data \
    -p 5555:5555 \
    --name redroid11 \
    redroid/redroid:11.0.0-latest \
    androidboot.use_memfd=1
```
