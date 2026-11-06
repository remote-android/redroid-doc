#!/bin/bash
# Entrypoint of the redroid-webrtc image: runs the cuttlefish operator (web UI
# and signaling) and one redroid_webrtc streamer per device. All processes
# share this container's network namespace, which the operator requires (it
# reaches each streamer's client files at 127.0.0.1).
#
# Devices come from REDROID_DEVICES and/or, with REDROID_DISCOVERY=docker,
# from running containers labelled "redroid.webrtc" (needs the docker socket
# mounted at /var/run/docker.sock).
#
# Environment:
#   REDROID_DEVICES   comma separated list of devices, each "name=host:port"
#                     or just "host:port" (name defaults to the host),
#                     e.g. "redroid1=172.30.0.11:5555,redroid2=172.30.0.12:5555"
#   REDROID_DISCOVERY "docker" to follow labelled containers. Labels:
#                     redroid.webrtc (any value, required),
#                     redroid.webrtc.name (device name, default: container
#                     name), redroid.webrtc.network (network to take the IP
#                     from, default: first one), redroid.webrtc.port (5555)
#   HTTPS_PORT        operator HTTPS port (default 8443, 0 disables)
#   HTTP_PORT         operator HTTP port (default 8080)
#   LISTEN_ADDR       operator listen address (default 0.0.0.0)
#   UDP_PORT_MIN/MAX  UDP port range for WebRTC media (default 15550-15599),
#                     shared by all streamers
#   STREAMER_ARGS     extra flags for every redroid_webrtc process
#   TLS_CERT_DIR      directory with cert.pem/key.pem (generated if missing)
#   IPC_DIR           directory with one subdirectory per device holding that
#                     device's /ipc (default /var/lib/redroid-webrtc/ipc).
#                     A device whose directory exists is streamed from its
#                     hwcomposer socket (--source=hwc, the redroid container
#                     needs androidboot.use_redroid_stream=1); others go
#                     through scrcpy's H.264 encoder.
set -euo pipefail

SHARE=/usr/share/redroid-webrtc
RUN_DIR=/run/redroid-webrtc
DOCKER_SOCK=/var/run/docker.sock
HTTPS_PORT="${HTTPS_PORT:-8443}"
HTTP_PORT="${HTTP_PORT:-8080}"
LISTEN_ADDR="${LISTEN_ADDR:-0.0.0.0}"
UDP_PORT_MIN="${UDP_PORT_MIN:-15550}"
UDP_PORT_MAX="${UDP_PORT_MAX:-15599}"
TLS_CERT_DIR="${TLS_CERT_DIR:-/var/lib/redroid-webrtc/cert}"
IPC_DIR="${IPC_DIR:-/var/lib/redroid-webrtc/ipc}"
REDROID_DISCOVERY="${REDROID_DISCOVERY:-}"
read -r -a EXTRA_ARGS <<< "${STREAMER_ARGS:-}"

if [ -z "${REDROID_DEVICES:-}" ] && [ "$REDROID_DISCOVERY" != "docker" ]; then
  echo "Set REDROID_DEVICES (e.g. redroid1=172.30.0.11:5555) or REDROID_DISCOVERY=docker" >&2
  exit 1
fi
if [ "$REDROID_DISCOVERY" = "docker" ] && [ ! -S "$DOCKER_SOCK" ]; then
  echo "REDROID_DISCOVERY=docker needs $DOCKER_SOCK mounted" >&2
  exit 1
fi

if [ ! -d /dev/dma_heap ]; then
  echo "WARNING: /dev/dma_heap missing on this host. redroid needs a kernel with" >&2
  echo "CONFIG_DMABUF_HEAPS for video encoding; the stream may stay black." >&2
fi

mkdir -p "$RUN_DIR"
operator_pid=""
declare -A streamer_pids=()    # device name -> pid of its supervisor
declare -A streamer_serials=() # device name -> adb serial
next_scrcpy_port=27183

stop_streamer() {
  local name="$1" pid="${streamer_pids[$1]:-}"
  if [ -n "$pid" ]; then
    echo "Stopping device $name"
    # Each supervisor runs in its own process group (setsid).
    kill -TERM -- "-$pid" 2>/dev/null || true
    unset "streamer_pids[$name]" "streamer_serials[$name]"
  fi
}

cleanup() {
  trap - TERM INT
  for name in "${!streamer_pids[@]}"; do
    stop_streamer "$name"
  done
  [ -n "$operator_pid" ] && kill -TERM "$operator_pid" 2>/dev/null
  wait || true
  exit 0
}
trap cleanup TERM INT

# Operator
operator_args=(--socket_path="$RUN_DIR/operator"
               --control_socket_path="$RUN_DIR/operator_control"
               --listen_addr="$LISTEN_ADDR" --http_port="$HTTP_PORT")
if [ "$HTTPS_PORT" != "0" ]; then
  if [ ! -f "$TLS_CERT_DIR/cert.pem" ] || [ ! -f "$TLS_CERT_DIR/key.pem" ]; then
    echo "Generating a self-signed TLS certificate in $TLS_CERT_DIR"
    mkdir -p "$TLS_CERT_DIR"
    openssl req -x509 -newkey rsa:2048 -nodes -days 3650 -subj "/CN=redroid-webrtc" \
      -keyout "$TLS_CERT_DIR/key.pem" -out "$TLS_CERT_DIR/cert.pem" 2>/dev/null
  fi
  operator_args+=(--https_port="$HTTPS_PORT" --tls_cert_dir="$TLS_CERT_DIR")
fi
(cd "$SHARE/operator" && exec operator "${operator_args[@]}") &
operator_pid=$!

for _ in $(seq 50); do
  [ -S "$RUN_DIR/operator" ] && break
  sleep 0.1
done

adb start-server >/dev/null 2>&1 || true

# Starts a supervised streamer (restarted when it exits) for one device.
start_streamer() {
  local name="$1" serial="$2" port="$next_scrcpy_port"
  next_scrcpy_port=$((next_scrcpy_port + 1))
  local source_args=()
  if [ -d "$IPC_DIR/$name" ]; then
    source_args=(--source=hwc --hwc_socket="$IPC_DIR/$name/hwcomposer.sock"
                 --camera_socket="$IPC_DIR/$name/camera.sock")
    echo "Device $name -> $serial (raw frames from $IPC_DIR/$name)"
  else
    echo "Device $name -> $serial (scrcpy H.264)"
  fi
  setsid bash -c '
    name="$1" serial="$2" port="$3"; shift 3
    trap "kill -TERM \$child 2>/dev/null; exit 0" TERM
    while true; do
      redroid_webrtc --device_id="$name" --adb_serial="$serial" \
        --operator_path="'"$RUN_DIR"'/operator" \
        --client_dir="'"$SHARE"'/html_client" \
        --scrcpy_server="'"$SHARE"'/scrcpy-server" --scrcpy_port="$port" \
        --mic_helper="'"$SHARE"'/mic-injector.dex" \
        --udp_port_min="'"$UDP_PORT_MIN"'" --udp_port_max="'"$UDP_PORT_MAX"'" \
        "$@" > >(sed -u "s/^/[$name] /") 2>&1 &
      child=$!
      wait $child || true
      echo "[$name] streamer exited, restarting in 5s" >&2
      sleep 5 & wait $!
    done' _ "$name" "$serial" "$port" ${source_args[@]+"${source_args[@]}"} \
      ${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"} &
  streamer_pids[$name]=$!
  streamer_serials[$name]="$serial"
}

# Static devices
IFS=',' read -r -a devices <<< "${REDROID_DEVICES:-}"
for device in "${devices[@]}"; do
  device="$(echo "$device" | xargs)"
  [ -z "$device" ] && continue
  if [[ "$device" == *=* ]]; then
    name="${device%%=*}"
    serial="${device#*=}"
  else
    serial="$device"
    name="${device%%:*}"
  fi
  [[ "$serial" == *:* ]] || serial="$serial:5555"
  start_streamer "$name" "$serial"
done
static_devices=" ${!streamer_pids[*]} "

# Prints "name serial" for each running container labelled redroid.webrtc.
discover_docker() {
  curl -sf --unix-socket "$DOCKER_SOCK" \
    'http://localhost/containers/json?filters=%7B%22label%22%3A%5B%22redroid.webrtc%22%5D%7D' |
  jq -r '.[] |
    (.Labels["redroid.webrtc.name"] // (.Names[0] | ltrimstr("/"))) as $name |
    (.Labels["redroid.webrtc.port"] // "5555") as $port |
    (.Labels["redroid.webrtc.network"]) as $net |
    (if $net then .NetworkSettings.Networks[$net].IPAddress
     else ([.NetworkSettings.Networks[].IPAddress | select(. != "")] | first)
     end) as $ip |
    select($ip != null and $ip != "") | "\($name) \($ip):\($port)"'
}

echo "Web UI: https://<host>:$HTTPS_PORT/ (or http://<host>:$HTTP_PORT/)"

if [ "$REDROID_DISCOVERY" != "docker" ]; then
  wait "$operator_pid"
  cleanup
fi

echo "Following containers labelled redroid.webrtc"
while kill -0 "$operator_pid" 2>/dev/null; do
  if found="$(discover_docker)"; then
    declare -A seen=()
    while read -r name serial; do
      [ -z "$name" ] && continue
      seen[$name]=1
      if [ "${streamer_serials[$name]:-}" != "$serial" ]; then
        stop_streamer "$name"
        start_streamer "$name" "$serial"
      fi
    done <<< "$found"
    for name in "${!streamer_pids[@]}"; do
      if [ -z "${seen[$name]:-}" ] && [[ "$static_devices" != *" $name "* ]]; then
        stop_streamer "$name"
      fi
    done
    unset seen
  else
    echo "Docker discovery failed (is $DOCKER_SOCK accessible?)" >&2
  fi
  sleep 5 & wait $! || true
done
cleanup
