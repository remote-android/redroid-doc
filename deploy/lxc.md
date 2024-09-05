# Deploy redroid via LXC

```
## take Ubuntu 22.04 as example here
## adjust for other Linux distros

## install required kernel modules
apt install linux-modules-extra-`uname -r`
modprobe binder_linux devices="binder,hwbinder,vndbinder"

## install lxc tools
apt install lxc-utils

## check lxc netowrking
systemctl --no-pager status lxc-net

## adjust oci template
sed -i 's/set -eu/set -u/g' /usr/share/lxc/templates/lxc-oci

## install required tools
apt install skopeo umoci jq

## create redroid container
lxc-create -n redroid11 -t oci -- -u docker://docker.io/redroid/redroid:12.0.0_64only-latest

## adjust container config
mkdir $HOME/data-redroid11
sed -i '/lxc.include/d' /var/lib/lxc/redroid11/config
<<EOF cat >> /var/lib/lxc/redroid11/config
### hacked
lxc.init.cmd = /init androidboot.hardware=redroid androidboot.redroid_gpu_mode=guest
lxc.apparmor.profile = unconfined
lxc.autodev = 1
lxc.autodev.tmpfs.size = 25000000
lxc.mount.entry = $HOME/data-redroid11 data none bind 0 0
EOF

## [workaround] for redroid networking
rm /var/lib/lxc/redroid11/rootfs/vendor/bin/ipconfigstore

## start redroid container
lxc-start -l debug -o redroid11.log -n redroid11

## check container info
lxc-info redroid11

## grab adb shell
adb connect `lxc-info redroid11 -i | awk '{print $2}'`:5555

## or execute sh directly
nsenter -t `lxc-info redroid11 -p | awk '{print $2}'` -a sh

## stop redroid container
lxc-stop -k redroid11
```
