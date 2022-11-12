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

getenforce &> getenforce.txt

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
    docker exec $1 /vendor/bin/vainfo -a > vainfo.txt
    docker exec $1 getprop > getprop.txt
    docker exec $1 dumpsys > dumpsys.txt

<<'EOF' >network.txt  docker exec -i $1 sh
    echo "************** ip a"
    ip a
    echo "************** ip rule"
    ip rule
    echo;echo "************** ip r show table eth0"
    ip r list table eth0
EOF
    docker container inspect $1 > container-inspect.txt
    docker image inspect `docker container inspect -f '{{.Config.Image}}' $1` > image-inspect.txt
fi

tmp_tar=${tmp_dir}.tgz
tar czf $tmp_tar $tmp_dir

echo "all logs collected in $tmp_tar"
