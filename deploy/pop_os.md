# Deploy redroid on Pop!_OS

```
##############################
## Pop!_OS 22.04
##############################

## install linux-xanmod (https://xanmod.org) to get binderfs support

## enable PSI in /etc/default/grub
## GRUB_CMDLINE_LINUX_DEFAULT="... psi=1"

## run redroid
docker run -itd --rm --privileged \
    --pull always \
    -v ~/data11:/data \
    -p 5555:5555 \
    --name redroid11 \
    redroid/redroid:12.0.0_64only-latest
```
