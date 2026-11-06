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

#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"

namespace redroid {

// Serves a client's webcam to the device's camera HAL (camera-hal/) on a unix
// socket, as raw 1280x720 I420 frames. The HAL connects while a camera is open.
class CameraServer : public rtc::VideoSinkInterface<webrtc::VideoFrame> {
 public:
  explicit CameraServer(std::string socket_path);
  ~CameraServer() override;

  bool Start();
  void OnFrame(const webrtc::VideoFrame& frame) override;

 private:
  void AcceptLoop();

  std::string socket_path_;
  int listen_fd_ = -1;
  std::atomic<bool> running_{false};
  std::thread thread_;
  std::mutex mutex_;
  int client_fd_ = -1;
  rtc::scoped_refptr<webrtc::I420Buffer> scaled_;
  std::string frame_;
};

}  // namespace redroid
