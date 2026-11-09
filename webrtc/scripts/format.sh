#!/bin/bash
# Formats the streamer sources with cuttlefish's pinned clang-format and style
# (the bazel build fails on formatting violations). Run on the build host after
# a dev-build.sh run (needs the bazel cache and the cuttlefish checkout).
#   webrtc/scripts/format.sh [cuttlefish-checkout]
set -euo pipefail
WEBRTC_DIR="$(cd "$(dirname "$0")/.." && pwd)"
CF_SRC="${1:-$HOME/src/android-cuttlefish}"
CLANG_FORMAT="$(cd "$CF_SRC/base/cvd" && bazel info output_base 2>/dev/null)/external/toolchains_llvm++llvm+llvm_toolchain_llvm/bin/clang-format"
"$CLANG_FORMAT" --style="file:$CF_SRC/base/cvd/.clang-format" -i \
  "$WEBRTC_DIR"/streamer/*.h "$WEBRTC_DIR"/streamer/*.cpp
