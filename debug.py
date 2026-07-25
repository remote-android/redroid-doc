#!/usr/bin/env python3
import shutil
import subprocess
import sys
import os
import time
import zipfile
from platform import uname
import gzip

usage = f"""
USAGE: {os.path.basename(sys.argv[0])} [container] [-h]

where:
container: container id or container name
"""


def p_usage():
    print(usage)
    sys.exit(1)

def main(container:str|None = None):
    if os.geteuid() != 0:
        print("\033[31mYou must be root to run this script.\033[0m")
        sys.exit(1)
    if not container:
        container = input("Container name (leave empty if stopped):")
    print("Collecting, please wait...")
    tmp_dir = f"redroid_report_{time.time()}"
    os.makedirs(tmp_dir)
    if os.path.exists(f"/boot/config-{uname().release}") and False:
        shutil.copyfile(f"/boot/config-{uname().release}", f"{tmp_dir}/config-{uname().release}")
    else:
        if os.path.exists("/proc/config.gz"):
            with gzip.GzipFile("/proc/config.gz") as f, open(f"{tmp_dir}/config-{uname().release}", 'wb', newline='\n') as g:
                g.write(f.read())
    with open(f"{tmp_dir}/drivers.txt","w", encoding='utf-8',newline='\n') as f, open("/proc/filesystems") as fs:
        for line in fs.readlines():
            if "binder" in line:
                f.write(line)
                break
    with open(f"{tmp_dir}/drivers.txt", "a", encoding='utf-8',newline='\n') as f, open("/proc/misc") as fs:
        for line in fs.readlines():
            if "ashmem" in line:
                f.write(line)
                break
    with open(f"{tmp_dir}/uname.txt", "w", encoding='utf-8', newline='\n') as f:
        f.write(" ".join(uname()))

    for c  in ['lscpu', 'getenforce']:
        with open(f"{tmp_dir}/{c}.txt", "w", encoding='utf-8', newline='\n') as f:
            ret = subprocess.check_output([c], stderr=subprocess.STDOUT)
            f.write(ret.decode("utf-8"))

    with open(f"{tmp_dir}/dri.txt", "w", encoding='utf-8', newline='\n') as f:
        for c in [["lshw", "-C", "display"],["ls", "-al", "/dev/dri/"]]:
            print("running:", c)
            ret = subprocess.check_output(c, stderr=subprocess.STDOUT, shell=True)
            f.write(ret.decode("utf-8"))
        for i in os.listdir("/sys/kernel/debug/dri"):
            if os.path.exists(f"/sys/kernel/debug/dri/{i}/name"):
                with open(f"/sys/kernel/debug/dri/{i}/name") as nf:
                    f.write(nf.read())

    with open(f"{tmp_dir}/dmesg.txt", "w", encoding='utf-8', newline='\n') as f:
        ret = subprocess.check_output(["dmesg", "-T"], stderr=subprocess.STDOUT)
        f.write(ret.decode("utf-8"))

    with open(f"{tmp_dir}/docker-info.txt", "w", encoding='utf-8', newline='\n') as f:
        ret = subprocess.check_output(["docker", "info"], stderr=subprocess.STDOUT)
        f.write(ret.decode("utf-8"))
    if container:
        with open(f"{tmp_dir}/ps.txt", "w", encoding='utf-8', newline='\n') as f:
            ret = subprocess.check_output(["docker", "exec", container, "ps", '-A'], stderr=subprocess.STDOUT)
            f.write(ret.decode("utf-8"))
        with open(f"{tmp_dir}/logcat.txt", "w", encoding='utf-8', newline='\n') as f:
            ret = subprocess.check_output(["docker", "exec", container, "logcat", '-d'], stderr=subprocess.STDOUT)
            f.write(ret.decode("utf-8"))
        with open(f"{tmp_dir}/crash.txt", "w", encoding='utf-8', newline='\n') as f:
            ret = subprocess.check_output(["docker", "exec", container, "logcat", '-d', '-b', 'crash'], stderr=subprocess.STDOUT)
            f.write(ret.decode("utf-8"))
        with open(f"{tmp_dir}/vainfo.txt", "w", encoding='utf-8', newline='\n') as f:
            ret = subprocess.check_output(["docker", "exec", container, "/vendor/bin/vainfo", '-a'], stderr=subprocess.STDOUT)
            f.write(ret.decode("utf-8"))
        with open(f"{tmp_dir}/getprop.txt", "w", encoding='utf-8', newline='\n') as f:
            ret = subprocess.check_output(["docker", "exec", container, "getprop"], stderr=subprocess.STDOUT)
            f.write(ret.decode("utf-8"))
        with open(f"{tmp_dir}/dumpsys.txt", "w", encoding='utf-8', newline='\n') as f:
            ret = subprocess.check_output(["docker", "exec", container, "dumpsys"], stderr=subprocess.STDOUT)
            f.write(ret.decode("utf-8"))
        with open(f"{tmp_dir}/network.txt", "w", encoding='utf-8', newline='\n') as f:
            f.write("-----ip a")
            ret = subprocess.check_output(["docker", "exec", container, "ip", 'a'], stderr=subprocess.STDOUT)
            f.write(ret.decode("utf-8"))
            f.write("-----ip rule")
            ret = subprocess.check_output(["docker", "exec", container, "ip", 'rule'], stderr=subprocess.STDOUT)
            f.write(ret.decode("utf-8"))
            f.write("-----ip r show table eth0")
            ret = subprocess.check_output(["docker", "exec", container, "ip", 'r', 'show', 'table', 'eth0'], stderr=subprocess.STDOUT)
            f.write(ret.decode("utf-8"))
        with open(f"{tmp_dir}/container-inspect.txt", "w", encoding='utf-8', newline='\n') as f:
            ret = subprocess.check_output(["docker", "container","inspect", container], stderr=subprocess.STDOUT)
            f.write(ret.decode("utf-8"))
    zipfile_ = os.path.basename(tmp_dir)+".zip"
    with zipfile.ZipFile(zipfile_, 'w') as zipObj:
        for file in os.listdir(tmp_dir):
            zipObj.write(os.path.join(tmp_dir, file))
    shutil.rmtree(tmp_dir)
    os.chown(zipfile_, 0o755, 1000)
    print("===================================")
    print("Please provide the collected logs")
    print(os.path.realpath(zipfile_))
    print("请提供此处收集的日志")
    print("===================================")

if __name__ == "__main__":
    if "-h" in sys.argv or "--help" in sys.argv:
        p_usage()
    main(sys.argv[1] if len(sys.argv)>=2 else None)
