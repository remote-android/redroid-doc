---
name: Bug report
about: Create a report to help us improve
title: ''
labels: ''
assignees: ''

---

**Describe the bug**
A clear and concise description of what the bug is.

**make sure the required kernel modules present**
- `grep binder /proc/filesystems`
- `grep ashmem /proc/misc`

**collect debug logs**
`curl -fsSL https://raw.githubusercontent.com/remote-android/redroid-doc/master/debug.sh | bash -s -- [CONTAINER]`
omit CONTAINER if not exist any more.

**Screenshots**
If applicable, add screenshots to help explain your problem.
