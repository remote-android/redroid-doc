# Deploy redroid on Deepin

```
##############################
## Deepin 23
##############################

### install podman or docker
apt install podman

### start redroid
mkdir ~/data11
podman run -itd --rm --privileged \
    --pull always \
    -v ~/data11:/data \
    -p 5555:5555 \
    --name redroid11 \
    redroid/redroid:12.0.0_64only-latest


##############################
## Deepin 20.9
##############################

chmod 666 /dev/{,hw,vnd}binder

### install podman: https://github.com/mgoltzsche/podman-static

### start redroid
mkdir ~/data11
podman run -itd --rm --privileged \
    --pull always \
    -v ~/data11:/data \
    -p 5555:5555 \
    --name redroid11 \
    redroid/redroid:12.0.0_64only-latest

### NOTE:
### You can start only one redroid container at the same time.
### Try enable `binderfs` if want to start unlimit redroid containers.
```
