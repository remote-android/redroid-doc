#!/bin/bash
# Development build of the redroid WebRTC streamer and the cuttlefish operator,
# run on a Linux host (e.g. the Lima VM). Produces a runnable tree in $OUT:
#   $OUT/bin/redroid_webrtc, $OUT/bin/operator, $OUT/mic-injector.dex,
#   $OUT/html_client/ (device page), $OUT/operator/{static,intercept}/
#
# Usage: webrtc/scripts/dev-build.sh [cuttlefish-checkout] [out-dir]
set -euo pipefail

WEBRTC_DIR="$(cd "$(dirname "$0")/.." && pwd)"
CF_SRC="${1:-$HOME/src/android-cuttlefish}"
OUT="${2:-$HOME/redroid-webrtc}"
CF_COMMIT="$(cat "$WEBRTC_DIR/CUTTLEFISH_VERSION")"
# scrcpy server pushed to the device (Apache-2.0, github.com/Genymobile/scrcpy)
SCRCPY_VERSION=3.3.1
SCRCPY_SHA256=a0f70b20aa4998fbf658c94118cd6c8dab6abbb0647a3bdab344d70bc1ebcbb8
# Compile tools of the device microphone helper (mic-helper/).
R8_VERSION=8.5.35
R8_SHA256=4733945987ee0a840fafc34080b135259e01678412e07212b23f706334290294
ANDROID_PLATFORM=platform-34-ext7_r03
ANDROID_PLATFORM_SHA256=16fdb74c55e59ae3ef52def135aec713508467bd56d7dabcd8c9be31fa8b20f3
# Bazel flags. Optimized by default: the -O0 default build is several times
# slower at decoding/converting/encoding video. E.g. BAZEL_FLAGS="-c opt --jobs=4"
# on small hosts (~/.bazelrc also applies).
read -r -a BAZEL_FLAGS <<< "${BAZEL_FLAGS:--c opt}"

if [ ! -d "$CF_SRC/.git" ]; then
  git clone --filter=blob:none https://github.com/google/android-cuttlefish.git "$CF_SRC"
fi
if [ "$(git -C "$CF_SRC" rev-parse HEAD)" != "$CF_COMMIT" ]; then
  git -C "$CF_SRC" fetch origin "$CF_COMMIT"
  git -C "$CF_SRC" checkout --detach "$CF_COMMIT"
fi

# Apply patches idempotently.
for p in "$WEBRTC_DIR"/patches/*.patch; do
  if git -C "$CF_SRC" apply --check "$p" 2>/dev/null; then
    echo "Applying $(basename "$p")"
    git -C "$CF_SRC" apply "$p"
  elif git -C "$CF_SRC" apply --reverse --check "$p" 2>/dev/null; then
    echo "Already applied: $(basename "$p")"
  else
    echo "Patch does not apply: $p" >&2
    exit 1
  fi
done

# Our sources live in the cuttlefish tree so they can depend on libdevice.
PKG="$CF_SRC/base/cvd/cuttlefish/host/frontend/redroid_webrtc"
rm -rf "$PKG"
mkdir -p "$PKG"
cp "$WEBRTC_DIR"/streamer/*.{h,cpp} "$WEBRTC_DIR"/streamer/BUILD.bazel "$PKG"/

(cd "$CF_SRC/base/cvd" &&
  bazel build ${BAZEL_FLAGS[@]+"${BAZEL_FLAGS[@]}"} //cuttlefish/host/frontend/redroid_webrtc:redroid_webrtc &&
  if [ -z "${SKIP_TESTS:-}" ]; then
    bazel test ${BAZEL_FLAGS[@]+"${BAZEL_FLAGS[@]}"} //cuttlefish/host/frontend/redroid_webrtc:all
  fi)

(cd "$CF_SRC/frontend/src/operator" && go build -o "$CF_SRC/frontend/out/operator" .)

mkdir -p "$OUT/bin" "$OUT/operator/static" "$OUT/operator/intercept"
if ! echo "$SCRCPY_SHA256  $OUT/scrcpy-server" | sha256sum -c --status 2>/dev/null; then
  wget -q -O "$OUT/scrcpy-server" \
    "https://github.com/Genymobile/scrcpy/releases/download/v$SCRCPY_VERSION/scrcpy-server-v$SCRCPY_VERSION"
  echo "$SCRCPY_SHA256  $OUT/scrcpy-server" | sha256sum -c
fi

TOOLS="$CF_SRC/.mic-helper-tools"
mkdir -p "$TOOLS"
if ! echo "$R8_SHA256  $TOOLS/r8.jar" | sha256sum -c --status 2>/dev/null; then
  wget -q -O "$TOOLS/r8.jar" \
    "https://dl.google.com/android/maven2/com/android/tools/r8/$R8_VERSION/r8-$R8_VERSION.jar"
  echo "$R8_SHA256  $TOOLS/r8.jar" | sha256sum -c
fi
if [ ! -f "$TOOLS/$ANDROID_PLATFORM.ok" ]; then
  wget -q -O "$TOOLS/platform.zip" \
    "https://dl.google.com/android/repository/$ANDROID_PLATFORM.zip"
  echo "$ANDROID_PLATFORM_SHA256  $TOOLS/platform.zip" | sha256sum -c
  unzip -qjo "$TOOLS/platform.zip" '*/android.jar' -d "$TOOLS"
  rm "$TOOLS/platform.zip"
  touch "$TOOLS/$ANDROID_PLATFORM.ok"
fi
rm -rf "$TOOLS/classes" "$TOOLS/dex"
mkdir -p "$TOOLS/classes" "$TOOLS/dex"
javac -nowarn --release 8 -cp "$TOOLS/android.jar" -d "$TOOLS/classes" \
  "$WEBRTC_DIR"/mic-helper/src/redroid/*.java
java -cp "$TOOLS/r8.jar" com.android.tools.r8.D8 --lib "$TOOLS/android.jar" \
  --min-api 30 --output "$TOOLS/dex" "$TOOLS"/classes/redroid/*.class
install -m 0644 "$TOOLS/dex/classes.dex" "$OUT/mic-injector.dex"

install -m 0755 "$CF_SRC/base/cvd/bazel-bin/cuttlefish/host/frontend/redroid_webrtc/redroid_webrtc" "$OUT/bin/"
install -m 0755 "$CF_SRC/frontend/out/operator" "$OUT/bin/"
rm -rf "$OUT/html_client"
cp -r "$CF_SRC/base/cvd/cuttlefish/host/frontend/webrtc/html_client" "$OUT/html_client"
rm -f "$OUT/html_client/BUILD.bazel"
cp -r "$CF_SRC/frontend/src/operator/intercept/." "$OUT/operator/intercept/"
cp -r "$WEBRTC_DIR/operator-static/." "$OUT/operator/static/"
echo "Built into $OUT"
