# Deploy redroid on Pop!_OS

```
## install linux-xanmod (https://xanmod.org) to get binderfs support


## enable cgroup v1
sudo kernelstub -a "systemd.unified_cgroup_hierarchy=0"
sudo update-initramfs -c -k all
sudo reboot


## run redroid
docker run -itd --rm --privileged \
    --pull always \
    -v ~/data11:/data \
    -p 5555:5555 \
    --name redroid11 \
    redroid/redroid:11.0.0-latest \
    androidboot.use_memfd=1
```
