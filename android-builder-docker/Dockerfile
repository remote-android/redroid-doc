FROM ubuntu:20.04

ARG userid
ARG groupid
ARG username

# COPY apt.conf /etc/apt/apt.conf

# COPY sources.list etc/apt/sources.list

ENV DEBIAN_FRONTEND noninteractive

RUN apt-get update \
    && echo "install package for building AOSP" \
    && apt-get install -y git-core gnupg flex bison build-essential zip curl zlib1g-dev \
        gcc-multilib g++-multilib libc6-dev-i386 libncurses5 lib32ncurses5-dev x11proto-core-dev \
        libx11-dev lib32z1-dev libgl1-mesa-dev libxml2-utils xsltproc unzip fontconfig \
    && echo "install utils" \
    && apt-get install -y sudo rsync \
    && echo "install packages for build mesa3d or meson related" \
    && apt-get install -y python3-pip pkg-config python3-dev ninja-build \
    && pip3 install mako meson \
    && echo "packages for legacy mesa3d (< 22.0.0)" \
    && apt-get install -y python2 python-mako python-is-python2 python-enum34 gettext


RUN groupadd -g $groupid $username \
    && useradd -m -u $userid -g $groupid $username \
    && echo "$username ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers \
    && echo $username >/root/username \
    && echo "$username:$username" | chpasswd && adduser $username sudo


ENV HOME=/home/$username \
    USER=$username \
    PATH=/src/.repo/repo:/src/prebuilts/jdk/jdk8/linux-x86/bin/:$PATH


ENTRYPOINT chroot --userspec=$(cat /root/username):$(cat /root/username) / /bin/bash -i
