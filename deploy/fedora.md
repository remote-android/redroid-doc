# Deploy redroid on Fedora

```
##############################
## Fedora 39
##############################

## disable selinux
grubby --update-kernel ALL --args selinux=0
grub2-mkconfig > /boot/grub2/grub.cfg
reboot

## add possible missing kernel modules
modprobe nfnetlink

## start redroid
docker run -itd --rm --privileged \
    --pull always \
    -v ~/data11:/data \
    -p 5555:5555 \
    --name redroid11 \
    redroid/redroid:12.0.0_64only-latest


##############################
## Fedora 38
##############################

## disable selinux
grubby --update-kernel ALL --args selinux=0
grub2-mkconfig > /boot/grub2/grub.cfg
reboot

## start redroid
docker run -itd --rm --privileged \
    --pull always \
    -v ~/data11:/data \
    -p 5555:5555 \
    --name redroid11 \
    redroid/redroid:12.0.0_64only-latest
```
