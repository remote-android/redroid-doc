FROM ubuntu:14.04

ARG userid
ARG groupid
ARG username

# COPY apt.conf /etc/apt/apt.conf

COPY sources.list etc/apt/sources.list

# gettext python-mako python3-mako used to build mesa3d
RUN apt-get update && apt-get install -y git-core gnupg flex bison gperf build-essential zip curl zlib1g-dev gcc-multilib g++-multilib libc6-dev-i386 lib32ncurses5-dev x11proto-core-dev libx11-dev lib32z-dev ccache libgl1-mesa-dev libxml2-utils xsltproc unzip python gettext python-mako python3-mako

RUN curl -o /usr/local/bin/repo https://storage.googleapis.com/git-repo-downloads/repo \
 && chmod a+x /usr/local/bin/repo

RUN groupadd -g $groupid $username \
 && useradd -m -u $userid -g $groupid $username \
 && echo $username >/root/username \
 && echo "$username:$username" | chpasswd && adduser $username sudo

ENV HOME=/home/$username \
    USER=$username \
    PATH=/src/.repo/repo:/src/prebuilts/jdk/jdk8/linux-x86/bin/:$PATH \
    USE_CCACHE=1 \
    CCACHE_DIR=/src/.ccache

ENTRYPOINT chroot --userspec=$(cat /root/username):$(cat /root/username) / /bin/bash -i
