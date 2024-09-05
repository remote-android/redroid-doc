# Deploy on Arch Linux

```
## install zen kernel
pacman -S linux-zen

## run redroid
docker run -itd --rm --privileged \
    --pull always \
    -v ~/data11:/data \
    -p 5555:5555 \
    --name redroid11 \
    redroid/redroid:12.0.0_64only-latest
```
