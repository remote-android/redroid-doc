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

#include "cuttlefish/host/frontend/redroid_webrtc/camera_server.h"

#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>

#include "absl/log/log.h"

namespace redroid {
namespace {

constexpr int kWidth = 1280;
constexpr int kHeight = 720;

void AppendPlane(std::string* out, const uint8_t* data, int stride, int width,
                 int height) {
  for (int y = 0; y < height; y++) {
    out->append(reinterpret_cast<const char*>(data + y * stride), width);
  }
}

}  // namespace

CameraServer::CameraServer(std::string socket_path)
    : socket_path_(std::move(socket_path)) {}

CameraServer::~CameraServer() {
  running_ = false;
  if (thread_.joinable()) {
    thread_.join();
  }
  if (listen_fd_ >= 0) {
    close(listen_fd_);
  }
  if (client_fd_ >= 0) {
    close(client_fd_);
  }
  unlink(socket_path_.c_str());
}

bool CameraServer::Start() {
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (socket_path_.size() >= sizeof(addr.sun_path)) {
    LOG(ERROR) << "Camera socket path too long: " << socket_path_;
    return false;
  }
  strcpy(addr.sun_path, socket_path_.c_str());
  listen_fd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  unlink(socket_path_.c_str());
  if (listen_fd_ < 0 ||
      bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
      listen(listen_fd_, 1) != 0) {
    PLOG(ERROR) << "Failed to listen on " << socket_path_;
    return false;
  }
  // The HAL runs as the device's cameraserver user.
  chmod(socket_path_.c_str(), 0666);
  running_ = true;
  thread_ = std::thread([this] { AcceptLoop(); });
  return true;
}

void CameraServer::AcceptLoop() {
  while (running_) {
    pollfd pfd{listen_fd_, POLLIN, 0};
    if (poll(&pfd, 1, 200) <= 0) {
      continue;
    }
    int fd = accept4(listen_fd_, nullptr, nullptr, SOCK_CLOEXEC);
    if (fd < 0) {
      continue;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (client_fd_ >= 0) {
      close(client_fd_);
    }
    client_fd_ = fd;
    LOG(INFO) << "Camera HAL connected";
  }
}

void CameraServer::OnFrame(const webrtc::VideoFrame& frame) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (client_fd_ < 0) {
    return;  // No camera open on the device.
  }
  auto input = frame.video_frame_buffer()->ToI420();
  if (!scaled_) {
    scaled_ = webrtc::I420Buffer::Create(kWidth, kHeight);
  }
  scaled_->CropAndScaleFrom(*input);
  frame_.clear();
  frame_.reserve(kWidth * kHeight * 3 / 2);
  AppendPlane(&frame_, scaled_->DataY(), scaled_->StrideY(), kWidth, kHeight);
  AppendPlane(&frame_, scaled_->DataU(), scaled_->StrideU(), kWidth / 2,
              kHeight / 2);
  AppendPlane(&frame_, scaled_->DataV(), scaled_->StrideV(), kWidth / 2,
              kHeight / 2);
  size_t sent = 0;
  while (sent < frame_.size()) {
    // A whole frame is always sent: the stream has no markers to resync on.
    ssize_t n = send(client_fd_, frame_.data() + sent, frame_.size() - sent,
                     MSG_NOSIGNAL);
    if (n <= 0) {
      close(client_fd_);
      client_fd_ = -1;
      LOG(INFO) << "Camera HAL disconnected";
      return;
    }
    sent += n;
  }
}

}  // namespace redroid
