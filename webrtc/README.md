# Web access to redroid (WebRTC)

Open your redroid containers in a web browser: live screen, touch, mouse
wheel, keyboard, hardware buttons, device audio and an adb console, with no
client to install.

This reuses the WebRTC stack of [Cuttlefish](https://github.com/google/android-cuttlefish)
(its operator, streaming library and HTML client) with **stock redroid
images, no rebuild needed**:

- **Screen:** raw frames from redroid's own hwcomposer stream socket
  (`androidboot.use_redroid_stream=1`, the default in `docker-compose.yml`),
  encoded on the host. Alternatively, H.264 from the device's encoder through
  [scrcpy](https://github.com/Genymobile/scrcpy).
- **Input and audio:** through a scrcpy server pushed to the device at runtime
  over adb.

```
 browser ── HTTPS/WebSocket (signaling) ──► redroid-webrtc container (host network)
    ▲                                       ├─ operator: web UI + signaling (:8443, :8080)
    └── WebRTC media (UDP 15550-15599) ─────┤─ redroid_webrtc (one per device)
                                            │    └─ adb ─► scrcpy-server on the device
                                            ▼
                          redroid1 (172.30.0.11:5555), redroid2, …
```

## Quick start

On a Linux host that already runs redroid (see the main [README](../README.md)):

```bash
git clone https://github.com/remote-android/redroid-doc.git
cd redroid-doc/webrtc
docker compose up -d
```

Then open `https://<host>:8443/` (self-signed certificate) or
`http://<host>:8080/`, and pick a device. The first build of the
`redroid-webrtc` image compiles Cuttlefish's WebRTC stack and takes about an
hour. Later builds use the cache.

### Host requirements

| Requirement | Why | Check |
|---|---|---|
| `binder_linux` module | redroid itself | `grep binder /proc/filesystems` |
| Kernel with `CONFIG_DMABUF_HEAPS` | Android's video encoder (used for streaming) allocates buffers there. Without it the screen stays black and no encoder is available. | `ls /dev/dma_heap` |
| Android 11+ image | audio capture (video and input work on older versions too) | |

Stock Ubuntu 22.04/24.04 kernels meet these. Some VM kernels don't. For
example, OrbStack has binder but no DMA-BUF heaps. On Apple Silicon Macs, use a
stock Ubuntu arm64 VM such as Lima, UTM or VMware Fusion.

## Configuration

`docker-compose.yml` runs one redroid container (`redroid1`) and the
`redroid-webrtc` container. To add devices, copy the `redroid1` service with
a new name and IP, then list it in `REDROID_DEVICES`:

```yaml
REDROID_DEVICES: "redroid1=172.30.0.11:5555,redroid2=172.30.0.12:5555"
```

### Automatic discovery

Instead of listing the devices, `redroid-webrtc` can follow the Docker
containers that have the `redroid.webrtc` label. Set
`REDROID_DISCOVERY=docker` and mount the Docker socket
(`/var/run/docker.sock:/var/run/docker.sock:ro`, which effectively grants
root on the host). Containers are checked every 5 seconds. A streamer starts
when a labelled container starts, and stops when it goes away.

```bash
docker run -itd --privileged --label redroid.webrtc=true \
  --label redroid.webrtc.name=pixel redroid/redroid:14.0.0_64only-latest
```

| Label | Default | |
|---|---|---|
| `redroid.webrtc` | | required, any value |
| `redroid.webrtc.name` | container name | device name in the web UI |
| `redroid.webrtc.network` | first network | network whose IP is used to reach adbd |
| `redroid.webrtc.port` | `5555` | adbd port |

Environment variables of `redroid-webrtc`:

| Variable | Default | Description |
|---|---|---|
| `REDROID_DEVICES` | | Comma separated `name=host:port` entries (`host:port` alone uses the host as name). Required unless discovery is on. |
| `REDROID_DISCOVERY` | | `docker` to follow labelled containers (see above) |
| `HTTPS_PORT` | `8443` | Web UI over HTTPS (`0` disables) |
| `HTTP_PORT` | `8080` | Web UI over plain HTTP |
| `LISTEN_ADDR` | `0.0.0.0` | Address the web UI listens on |
| `UDP_PORT_MIN` / `UDP_PORT_MAX` | `15550` / `15599` | UDP ports for WebRTC media, shared by all devices |
| `TLS_CERT_DIR` | `/var/lib/redroid-webrtc/cert` | `cert.pem` and `key.pem`. A self-signed pair is generated if missing. |
| `ICE_SERVERS` | Google STUN | JSON array of STUN/TURN servers, see [Remote access and NAT](#remote-access-and-nat) |
| `STREAMER_ARGS` | | Extra `redroid_webrtc` flags for every device, e.g. `--video_bit_rate=4000000 --fps=60 --audio=false` |

### Screen sources

| | hwcomposer stream (default) | scrcpy H.264 | scrcpy H.264 + `--video_passthrough` |
|---|---|---|---|
| Device side | `androidboot.use_redroid_stream=1` and `/ipc` shared with redroid-webrtc | nothing | nothing |
| Encoding | VP8 on the host | H.264 on the device, decode plus VP8 on the host | H.264 on the device, sent as is |
| CPU, 720p30 animating, one viewer: streamer / device container | ~20% / ~116% | ~33% / ~260% | ~7% / ~260% |
| Needs `/dev/dma_heap` (Codec2) for video | no | yes | yes |

The hwcomposer stream is undocumented redroid functionality, tested with the
redroid 14 image. Each frame's gralloc buffer is passed over
`/ipc/hwcomposer.sock` and acknowledged by the reader; see
`streamer/hwc_frame_source.h`. In `REDROID_DEVICES` mode, a device uses it
when `IPC_DIR/<name>` (`./data/ipc/<name>` in the compose file) exists.

### H.264 passthrough

With the scrcpy source, the device's H.264 stream is by default decoded and
re-encoded to VP8 for the browser. With `STREAMER_ARGS=--video_passthrough`, the device's H.264 goes to
the browser unchanged:

| 720p30, screen animating, one viewer (arm64) | CPU of the streamer |
|---|---|
| default (decode + VP8 encode) | ~33% of a core |
| `--video_passthrough` | ~7% of a core |

It also avoids a second lossy encode and its latency. All current browsers
support H.264 in WebRTC. The bit rate is fixed (`--video_bit_rate`, default
8 Mbps) rather than adapted to the network, and keyframes come from the device
whenever a viewer joins or reports loss.

Why host networking: browsers connect to the WebRTC media ports on the
addresses the streamer advertises, so the streamer must see the host's real
addresses. It reaches the redroid containers via their fixed IPs on the
`redroid` bridge network. The web UI and streamers must share one network
namespace, because the operator reaches each streamer on `127.0.0.1`.

### Remote access and NAT

On a LAN, the published UDP range is enough. By default, Google's public STUN
server is offered as well. Across NATs or firewalls, or where that server is
blocked, run your own STUN/TURN server (for example coturn) and set
`ICE_SERVERS` to a JSON array in WebRTC's `iceServers` format, with `urls`
always an array:

```bash
export ICE_SERVERS='[{"urls":["stun:turn.example.com:3478"]},
  {"urls":["turn:turn.example.com:3478?transport=udp","turns:turn.example.com:443?transport=tcp"],
   "username":"redroid","credential":"secret"}]'
docker compose up -d
```

Browsers fetch this list from the web UI, so anyone who can open the web UI can
read the TURN credentials. Streamers still try Google's STUN server too; if it
is blocked, that attempt just fails without affecting the other servers.

## Security

The web UI has **no authentication** of its own. Anyone who can reach it
controls the devices, including a full adb shell from the browser. Keep it on
a trusted network, or use the `auth` profile. It binds the web UI to
localhost and puts a Caddy reverse proxy with HTTPS and a password in front of it:

```bash
export AUTH_PASSWORD_HASH="$(docker run --rm caddy:2.8.4 caddy hash-password -p 'your-password')"
LISTEN_ADDR=127.0.0.1 HTTPS_PORT=0 docker compose --profile auth up -d
# https://<host>:9443/  user: admin (AUTH_USER), certificate from Caddy's local CA
# AUTH_SITE=android.example.com gets a public certificate instead (ports 80/443)
```

Other options, such as oauth2-proxy, Tailscale or Cloudflare Access, work the
same way in front of `127.0.0.1:8080`. Only signaling goes through the proxy;
media flows over the UDP port range. Don't publish the redroid containers'
adb port (5555) either.

## Features

| | Status |
|---|---|
| Screen (H.264 from the device, re-encoded to VP8, or passed through as H.264) | ✅ |
| Touch, multi-touch, mouse, wheel scroll | ✅ |
| Keyboard (click the screen first, US layout key codes, modifiers) | ✅ |
| Power, back, home, overview (menu), volume buttons | ✅ |
| Device audio (Android 11+) | ✅ |
| Microphone (mic button, HTTPS only, no image change, see below) | ✅ |
| adb console in the browser (Cuttlefish's adb.js over a data channel) | ✅ |
| Multiple devices, device list page | ✅ |
| Automatic discovery of labelled Docker containers | ✅ |
| Auto reconnect after device restarts | ✅ |
| Device rotation (stream and touch follow the device orientation) | ✅ |
| Webcam (optional image layer, Android 14, see [camera-hal](camera-hal/README.md)) | ✅ |
| Sensors, location, bluetooth | ❌ (not implemented) |

### Microphone

Click the mic button on the device page to turn it on (click again to turn it
off) and allow the
browser to use the
microphone. Browsers only offer it to secure pages, so use
`https://<host>:8443/` rather than the plain HTTP port.

Apps then hear your microphone on their normal sources (mic, voice
communication, voice recognition, camcorder, unprocessed). While audio arrives,
the streamer runs a small helper (`mic-helper/`) on the device through adb, and
it stops 5 seconds after the audio does. With several clients, their
microphones are mixed. Set `--mic_helper=` in `STREAMER_ARGS` to turn this off.

### Tested with

| redroid image (arm64, stock Ubuntu 24.04 kernel, no GPU) | Result |
|---|---|
| `12.0.0_64only` | ✅ video, audio, input |
| `13.0.0_64only` | ✅ video, audio, input |
| `14.0.0_64only` | ✅ video, audio, input, rotation, passthrough |
| `16.0.0_64only` | ❌ Android doesn't boot on this host: SurfaceFlinger aborts with `output buffer not gpu writeable` in software rendering. That's a redroid issue, not specific to WebRTC. |

## Troubleshooting

* **Black screen / "Could not connect to the scrcpy server" in the logs.**
  Check `/dev/dma_heap` on the host (see host requirements), then run
  `adb connect <ip>:5555 && adb shell getprop sys.boot_completed`.
* **"Video stream unavailable … H.264 encoder" in the logs.** The image has no
  usable H.264 encoder. That's usually the same DMA-BUF heap issue.
* **The page loads but video never starts.** WebRTC can't reach the UDP
  ports. Open `UDP_PORT_MIN`–`UDP_PORT_MAX` on the host firewall, or set up
  TURN for NAT.
* **Audio is very quiet or silent.** Android applies the media volume to the
  captured audio, and its default (5 of 15) makes it about 33 dB quieter. The
  first time a device is streamed, the streamer raises the media volume to
  the maximum. You'll see the volume panel flash on the device. It remembers
  that in the `redroid_webrtc_capture_volume_set` global setting, so your
  own changes are kept afterwards. To redo it, run
  `adb shell settings delete global redroid_webrtc_capture_volume_set`. To
  disable it, add `--raise_capture_volume=false` to `STREAMER_ARGS`. Use the
  volume buttons in the web UI's sidebar to adjust the volume.
* **Logs:** `docker compose logs -f redroid-webrtc`. Each line is prefixed
  with the device name.

## Development

`streamer/` holds the C++ streamer (`redroid_webrtc`). It builds inside the
android-cuttlefish Bazel workspace, pinned in `CUTTLEFISH_VERSION`, with the
patches in `patches/`. On a Linux host (Ubuntu 24.04):

```bash
scripts/dev-build.sh            # clone/patch cuttlefish, build, test, install to ~/redroid-webrtc
scripts/format.sh               # clang-format with cuttlefish's style (the build checks it)
docker buildx build -f docker/Dockerfile -t redroid/webrtc \
  --build-context artifacts=$HOME/redroid-webrtc .   # package a dev build
```

Run a streamer by hand against the operator from the dev build:

```bash
cd ~/redroid-webrtc/operator && ../bin/operator --socket_path=/tmp/op --http_port=8080 &
~/redroid-webrtc/bin/redroid_webrtc --device_id=dev --operator_path=/tmp/op \
  --client_dir=$HOME/redroid-webrtc/html_client \
  --scrcpy_server=$HOME/redroid-webrtc/scrcpy-server --adb_serial=172.17.0.2:5555
# --source=test_pattern streams a test pattern without any device
```

Source layout:

| Path | |
|---|---|
| `streamer/main.cpp` | flags, WebRTC setup, reconnect loop, frame repeater, audio chunking |
| `streamer/scrcpy_session.*` | starts scrcpy-server through adb, reads the video, audio and control sockets |
| `streamer/scrcpy_protocol.*` | scrcpy 3.x wire format (tested in `scrcpy_protocol_test.cpp`) |
| `streamer/h264_decoder.*` | FFmpeg H.264 decoding to frames for the streamer |
| `streamer/redroid_connection_observer.*` | browser input to scrcpy control messages, adb channel |
| `streamer/keymap.*` | Linux to Android key codes and meta state |
| `patches/` | small changes to Cuttlefish, also suitable for upstreaming |
| `operator-static/` | the device list page |

## Licenses

redroid-webrtc is under the Apache License 2.0. It builds on Cuttlefish
(Apache 2.0), libwebrtc (BSD), scrcpy-server (Apache 2.0, downloaded at build
time with a pinned checksum) and FFmpeg (LGPL 2.1+, linked dynamically from
the distribution packages).
