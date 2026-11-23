/*
 * Copyright (C) 2026 The redroid Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "cuttlefish/host/frontend/redroid_webrtc/scrcpy_session.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <random>

#include "absl/log/log.h"
#include "fmt/format.h"

namespace redroid {
namespace {

constexpr auto kDeviceServerPath = "/data/local/tmp/redroid-scrcpy-server.jar";

int ConnectLocal(int port) {
  int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    return -1;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    close(fd);
    return -1;
  }
  int one = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  return fd;
}

bool ReadFully(int fd, uint8_t* buf, size_t len) {
  while (len > 0) {
    ssize_t n = TEMP_FAILURE_RETRY(recv(fd, buf, len, 0));
    if (n <= 0) {
      return false;
    }
    buf += n;
    len -= n;
  }
  return true;
}

bool WriteFully(int fd, const uint8_t* buf, size_t len) {
  while (len > 0) {
    ssize_t n = TEMP_FAILURE_RETRY(send(fd, buf, len, MSG_NOSIGNAL));
    if (n <= 0) {
      return false;
    }
    buf += n;
    len -= n;
  }
  return true;
}

}  // namespace

ScrcpySession::ScrcpySession(const Adb& adb, Options options,
                             Callbacks callbacks)
    : adb_(adb),
      options_(std::move(options)),
      callbacks_(std::move(callbacks)) {
  std::random_device rd;
  scid_ = rd() & 0x7fffffff;
}

ScrcpySession::~ScrcpySession() { Stop(); }

bool ScrcpySession::Start() {
  stopping_ = false;
  disconnected_notified_ = false;
  if (!adb_.Run({"push", options_.server_path, kDeviceServerPath})) {
    LOG(ERROR) << "Failed to push " << options_.server_path;
    return false;
  }
  std::string socket_name = fmt::format("localabstract:scrcpy_{:08x}", scid_);
  // Remove a stale forward on our port (e.g. from a previous session).
  adb_.Run({"forward", "--remove", fmt::format("tcp:{}", options_.local_port)},
           /*quiet=*/true);
  if (!adb_.Run({"forward", fmt::format("tcp:{}", options_.local_port),
                 socket_name})) {
    LOG(ERROR) << "adb forward failed";
    return false;
  }

  std::string server_cmd = fmt::format(
      "CLASSPATH={} app_process / com.genymobile.scrcpy.Server {} "
      "scid={:08x} log_level=info tunnel_forward=true "
      "video={} video_codec=h264 video_bit_rate={} max_fps={} "
      "audio={} audio_codec=raw control=true clipboard_autosync=false "
      "send_device_meta=true send_frame_meta=true send_dummy_byte=true "
      "send_codec_meta=true cleanup=true power_on=true stay_awake=false",
      kDeviceServerPath, options_.server_version, scid_,
      options_.video ? "true" : "false", options_.video_bit_rate,
      options_.max_fps, options_.audio ? "true" : "false");
  server_pid_ = adb_.Spawn({"shell", server_cmd});
  if (server_pid_ < 0) {
    return false;
  }

  if (!ConnectSockets()) {
    Stop();
    return false;
  }

  if (video_fd_ >= 0) {
    threads_.emplace_back([this] { VideoLoop(); });
  }
  if (audio_fd_ >= 0) {
    threads_.emplace_back([this] { AudioLoop(); });
  }
  threads_.emplace_back([this] { ControlReadLoop(); });
  return true;
}

bool ScrcpySession::ConnectSockets() {
  // The server accepts the enabled streams in this order and sends a dummy
  // byte and the device name on the first one.
  std::vector<int*> sockets;
  if (options_.video) {
    sockets.push_back(&video_fd_);
  }
  if (options_.audio) {
    sockets.push_back(&audio_fd_);
  }
  sockets.push_back(&control_fd_);

  // The forward accepts connections before the server listens; retry until
  // the server sends its dummy byte on the first socket.
  int first = -1;
  for (int attempt = 0; attempt < 100 && !stopping_; attempt++) {
    int fd = ConnectLocal(options_.local_port);
    if (fd >= 0) {
      uint8_t dummy;
      if (ReadFully(fd, &dummy, 1)) {
        first = fd;
        break;
      }
      close(fd);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (first < 0) {
    LOG(ERROR) << "Could not connect to the scrcpy server";
    return false;
  }
  *sockets[0] = first;
  for (size_t i = 1; i < sockets.size(); i++) {
    *sockets[i] = ConnectLocal(options_.local_port);
    if (*sockets[i] < 0) {
      LOG(ERROR) << "Could not connect the scrcpy sockets";
      return false;
    }
  }

  uint8_t name[scrcpy::kDeviceNameLength];
  if (!ReadFully(first, name, sizeof(name))) {
    LOG(ERROR) << "Failed to read the scrcpy device name";
    return false;
  }
  device_name_ = scrcpy::ParseDeviceName(name);

  if (options_.video) {
    uint8_t video_header[scrcpy::kVideoHeaderLength];
    if (!ReadFully(video_fd_, video_header, sizeof(video_header))) {
      LOG(ERROR) << "Failed to read the scrcpy video header";
      return false;
    }
    video_header_ = scrcpy::ParseVideoHeader(video_header);
    if (video_header_.codec != scrcpy::kCodecH264) {
      LOG(ERROR) << "Video stream unavailable (codec id " << video_header_.codec
                 << "), does the device have an H.264 encoder?";
      return false;
    }
    LOG(INFO) << "scrcpy connected to '" << device_name_ << "' "
              << video_header_.width << "x" << video_header_.height;
  } else {
    LOG(INFO) << "scrcpy connected to '" << device_name_
              << "' (audio and control only)";
  }

  if (audio_fd_ >= 0) {
    uint8_t codec[4];
    if (!ReadFully(audio_fd_, codec, sizeof(codec))) {
      LOG(ERROR) << "Failed to read the audio header";
      return false;
    }
    uint32_t audio_codec = scrcpy::ParseU32(codec);
    if (audio_codec != scrcpy::kCodecRaw) {
      LOG(WARNING) << "Audio unavailable on this device (codec id "
                   << audio_codec << "), continuing without audio";
      close(audio_fd_);
      audio_fd_ = -1;
    }
  }
  return true;
}

void ScrcpySession::VideoLoop() {
  std::vector<uint8_t> payload;
  while (!stopping_) {
    uint8_t header_buf[scrcpy::kPacketHeaderLength];
    if (!ReadFully(video_fd_, header_buf, sizeof(header_buf))) {
      break;
    }
    auto header = scrcpy::ParsePacketHeader(header_buf);
    payload.resize(header.size);
    if (!ReadFully(video_fd_, payload.data(), payload.size())) {
      break;
    }
    if (callbacks_.on_video_packet) {
      callbacks_.on_video_packet(header, payload.data(), payload.size());
    }
  }
  NotifyDisconnected();
}

void ScrcpySession::AudioLoop() {
  std::vector<uint8_t> payload;
  while (!stopping_) {
    uint8_t header_buf[scrcpy::kPacketHeaderLength];
    if (!ReadFully(audio_fd_, header_buf, sizeof(header_buf))) {
      break;
    }
    auto header = scrcpy::ParsePacketHeader(header_buf);
    payload.resize(header.size);
    if (!ReadFully(audio_fd_, payload.data(), payload.size())) {
      break;
    }
    if (!header.config && callbacks_.on_audio_packet) {
      callbacks_.on_audio_packet(payload.data(), payload.size());
    }
  }
  NotifyDisconnected();
}

void ScrcpySession::ControlReadLoop() {
  // Device messages (clipboard, uhid acks) are not used; drain them so the
  // device never blocks writing them.
  uint8_t buf[4096];
  while (!stopping_) {
    ssize_t n = TEMP_FAILURE_RETRY(recv(control_fd_, buf, sizeof(buf), 0));
    if (n <= 0) {
      break;
    }
  }
  NotifyDisconnected();
}

bool ScrcpySession::SendControl(const std::vector<uint8_t>& message) {
  std::lock_guard<std::mutex> lock(control_mutex_);
  if (control_fd_ < 0) {
    return false;
  }
  return WriteFully(control_fd_, message.data(), message.size());
}

void ScrcpySession::NotifyDisconnected() {
  if (stopping_ || disconnected_notified_.exchange(true)) {
    return;
  }
  LOG(WARNING) << "scrcpy session disconnected";
  if (callbacks_.on_disconnected) {
    callbacks_.on_disconnected();
  }
}

void ScrcpySession::CloseSockets() {
  std::lock_guard<std::mutex> lock(control_mutex_);
  for (int* fd : {&video_fd_, &audio_fd_, &control_fd_}) {
    if (*fd >= 0) {
      shutdown(*fd, SHUT_RDWR);
    }
  }
}

void ScrcpySession::Stop() {
  stopping_ = true;
  CloseSockets();
  for (auto& thread : threads_) {
    if (thread.joinable()) {
      thread.join();
    }
  }
  threads_.clear();
  {
    std::lock_guard<std::mutex> lock(control_mutex_);
    for (int* fd : {&video_fd_, &audio_fd_, &control_fd_}) {
      if (*fd >= 0) {
        close(*fd);
        *fd = -1;
      }
    }
  }
  if (server_pid_ > 0) {
    kill(server_pid_, SIGTERM);
    waitpid(server_pid_, nullptr, 0);
    server_pid_ = -1;
  }
  adb_.Run({"forward", "--remove", fmt::format("tcp:{}", options_.local_port)},
           /*quiet=*/true);
}

}  // namespace redroid
