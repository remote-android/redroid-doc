---
name: Bug report
about: Create a report to help us improve
title: ''
labels: ''
assignees: ''

---

**Describe the bug**
**请详细描述问题（ZH_CN）**
A clear and concise description of what the bug is.

**make sure the required kernel modules present**
**确保必须的内核功能已开启（ZH_CN）**
- [ ] `grep binder /proc/filesystems`
- [ ] `grep ashmem /proc/misc` (optional)

**Collect debug logs**
**收集调试日志（ZH_CN）**
`curl -fsSL https://raw.githubusercontent.com/remote-android/redroid-doc/master/debug.sh | sudo bash -s -- [CONTAINER]`
omit CONTAINER if not exist any more.
如容器已退出，可忽略CONTAINER参数（ZH_CN）

**Screenshots**
**截图（ZH_CN）**
If applicable, add screenshots to help explain your problem.
