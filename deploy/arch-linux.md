# Deploy on Arch Linux

```
## install zen kernel
pacman -S linux-zen
mkdir -p /dev/binderfs
mount -t binder binder /dev/binderfs
## run redroid
docker run -it --rm --privileged --pull always -v ~/data11:/data -p 5555:5555 --name redroid11 redroid/redroid:12.0.0_64only-latest
```
