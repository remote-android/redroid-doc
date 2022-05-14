#!/bin/bash

usage() {
    script=`basename "$0"`
    echo "USAGE: $script [container]"
	echo;echo
}
usage

tmp_dir=`mktemp -d`
echo "creating tmp dir: $tmp_dir"

cd $tmp_dir

cp /boot/config-`uname -r` ./ || zcat /proc/config.gz > config-`uname -r`

check_drivers() {
    grep binder /proc/filesystems
    grep ashmem /proc/misc
}
check_drivers > drivers.txt

uname -a > uname.txt

lscpu > lscpu.txt

check_gpu() {
    lshw -C display
    ls -al /dev/dri/
    cat /sys/kernel/debug/dri/*/name
}
check_gpu > dri.txt

dmesg -T > dmesg.txt

docker info > docker-info.txt

if [ ! -z $1 ]; then
    docker exec $1 ps -A > ps.txt
    docker exec $1 logcat -d > logcat.txt
    docker exec $1 logcat -d -b crash > crash.txt
    docker exec $1 getprop > getprop.txt
    docker inspect $1 > docker-inspect.txt
fi

tmp_tar=${tmp_dir}.tgz
tar czf $tmp_tar $tmp_dir

echo "all logs collected in $tmp_tar"
