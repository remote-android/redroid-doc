---
name: Bug report
about: Create a report to help us improve
title: ''
labels: ''
assignees: ''

---

**Describe the bug**
A clear and concise description of what the bug is.

**Check required kernel modules**
- `grep binder /proc/filesystems`
- `grep ashmem /proc/misc`

**Host info**
- `uname -a`
- `lscpu`
- `ls -al /dev/dri/ && cat /sys/kernel/debug/dri/*/name`
- `docker info`
- `dmesg -T`  (suggest put into a text file)

**Container info**
- running command: `docker run ...`
- `docker exec <container> ps -A`
- `docker exec <container> logcat -d` (suggest put into a text file)

**Screenshots**
If applicable, add screenshots to help explain your problem.
