#!/usr/bin/env bash

usage="
USAGE: $(basename "$0") [container] [-h]

where:
container: container id or container name
"

while getopts ':h' option; do
    case "$option" in
        *) echo "$usage" ;exit 0;;
    esac
done

container=$1

if [[ -z "$container" ]]; then
    echo -n "Container name (leave empty if stopped):"
    read -r container < /dev/tty
fi

echo "Collecting, please wait..."

tmp_dir=$(mktemp -d -t redroid-debug.XXXXXXXX)
#echo "creating tmp dir: $tmp_dir"

cd "$tmp_dir" || exit 1

{ cp /boot/config-"$(uname -r)" ./ || zcat /proc/config.gz > config-"$(uname -r)"; } &> /dev/null

{ grep binder /proc/filesystems; grep ashmem /proc/misc; } > drivers.txt

uname -a &> uname.txt

lscpu &> lscpu.txt

getenforce &> getenforce.txt

{
    lshw -C display
    ls -al /dev/dri/
    cat /sys/kernel/debug/dri/*/name;
} &> dri.txt

dmesg -T > dmesg.txt

docker info &> docker-info.txt

if [[ -n $container ]]; then
    docker exec "$container" ps -A &> ps.txt
    docker exec "$container" logcat -d &> logcat.txt
    docker exec "$container" logcat -d -b crash &> crash.txt
    docker exec "$container" /vendor/bin/vainfo -a &> vainfo.txt
    docker exec "$container" getprop &> getprop.txt
    docker exec "$container" dumpsys &> dumpsys.txt

<<'EOF' >network.txt  docker exec -i "$container" sh
    echo "************** ip a"
    ip a
    echo "************** ip rule"
    ip rule
    echo;echo "************** ip r show table eth0"
    ip r list table eth0
EOF
    docker container inspect "$container" &> container-inspect.txt
    docker image inspect "$(docker container inspect -f '{{.Config.Image}}' "$container")" &> image-inspect.txt
fi

tmp_tar=${tmp_dir}.tgz
tar czf "$tmp_tar" "$tmp_dir" &> /dev/null

echo
echo "==================================="
echo "Please provide the collected logs"
echo "(zh_CN) 请提供此处收集的日志"
echo "${tmp_tar}"
echo "==================================="
