# redroid-webrtc: browser access to redroid containers through the cuttlefish
# WebRTC operator. Build from the webrtc/ directory:
#
#   docker buildx build -f docker/Dockerfile -t redroid/webrtc .
#
# The build stage compiles android-cuttlefish's WebRTC stack with bazel, which
# takes a while (about 1h on 6 cores, a few minutes with a warm cache). To
# package an existing scripts/dev-build.sh output instead, override the
# artifacts stage:
#
#   docker buildx build -f docker/Dockerfile -t redroid/webrtc \
#     --build-context artifacts=$HOME/redroid-webrtc .

ARG UBUNTU=ubuntu:24.04

FROM ${UBUNTU} AS build
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential ca-certificates clang cmake curl file git golang-go lld \
      python3 unzip wget xxd zip pkgconf protobuf-compiler \
      libaom-dev libavcodec-dev libavutil-dev libcap-dev libclang-dev \
      libcurl4-openssl-dev libfmt-dev libgbm-dev libgflags-dev \
      libgoogle-glog-dev libgtest-dev libjsoncpp-dev liblzma-dev libopus-dev \
      libprotobuf-c-dev libprotobuf-dev libsrtp2-dev libssl-dev \
      libwayland-dev libxml2-dev libz3-dev uuid-dev openjdk-17-jdk-headless \
    && rm -rf /var/lib/apt/lists/*
RUN arch="$(dpkg --print-architecture)" \
    && wget -q -O /tmp/bazelisk.deb \
      "https://github.com/bazelbuild/bazelisk/releases/download/v1.29.0/bazelisk-${arch}.deb" \
    && apt-get install -y /tmp/bazelisk.deb && rm /tmp/bazelisk.deb
RUN useradd -m -u 1001 builder && mkdir /out && chown builder /out
USER builder
COPY --chown=builder . /home/builder/webrtc
# Set to an empty value to run the unit tests during the build (CI does).
ARG SKIP_TESTS=1
RUN --mount=type=cache,target=/home/builder/.cache,uid=1001,gid=1001 \
    SKIP_TESTS="${SKIP_TESTS}" /home/builder/webrtc/scripts/dev-build.sh \
      /home/builder/android-cuttlefish /out

FROM scratch AS artifacts
COPY --from=build /out/ /

FROM ${UBUNTU}
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      adb ca-certificates curl jq libavcodec60 libopus0 openssl tini \
    && rm -rf /var/lib/apt/lists/*
COPY --from=artifacts /bin/redroid_webrtc /bin/operator /usr/bin/
COPY --from=artifacts /html_client /usr/share/redroid-webrtc/html_client
COPY --from=artifacts /operator /usr/share/redroid-webrtc/operator
COPY --from=artifacts /scrcpy-server /usr/share/redroid-webrtc/scrcpy-server
COPY --from=artifacts /mic-injector.dex /usr/share/redroid-webrtc/mic-injector.dex
COPY docker/entrypoint.sh /usr/bin/redroid-webrtc-entrypoint
EXPOSE 8443/tcp 8080/tcp 15550-15599/udp
ENTRYPOINT ["/usr/bin/tini", "--", "/usr/bin/redroid-webrtc-entrypoint"]
