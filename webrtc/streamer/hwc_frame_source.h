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

// Raw frames from redroid's hwcomposer.
//
// With androidboot.use_redroid_stream=1, hwcomposer.redroid listens on
// /ipc/hwcomposer.sock (mount /ipc to share it) and sends every composed
// frame to the connected client, as observed with redroid 12-14:
//   - one record of kRecordSize bytes per frame: the gralloc buffer handle
//     (native_handle_t + gralloc.redroid's private_handle_t ints) plus
//     padding, with the buffer's fd attached (SCM_RIGHTS);
//   - the buffer is a memfd holding RGBA_8888 pixels, stride = width
//     rounded up to 2 pixels;
//   - the next frame is only sent after the client writes one byte back.
// No video encoder runs on the device; frames are copied, then acknowledged.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "cuttlefish/host/libs/screen_connector/video_frame_buffer.h"

namespace redroid {

// Parsed frame record.
struct HwcFrameRecord {
  int num_fds = 0;
  int size = 0;    // buffer size in bytes
  int offset = 0;  // pixel data offset in the buffer
};

constexpr size_t kHwcRecordSize = 65;
constexpr int kGrallocHandleMagic = 0x3141592;

// Parses a record (kHwcRecordSize bytes). nullopt if it is not a gralloc
// private_handle_t from gralloc.redroid.
std::optional<HwcFrameRecord> ParseHwcFrameRecord(const uint8_t* data,
                                                  size_t size);

class HwcFrameSource {
 public:
  using FrameCallback =
      std::function<void(std::shared_ptr<cuttlefish::VideoFrameBuffer>)>;

  // width/height: the display size (androidboot.redroid_width/height).
  HwcFrameSource(std::string socket_path, int width, int height,
                 FrameCallback on_frame);
  ~HwcFrameSource();

  // Connects (retrying while the device is down) and streams frames from a
  // background thread until destroyed.
  void Start();
  bool connected() const { return connected_; }

  // Display rotation in degrees (0, 90, 180, 270, Android's
  // mCurrentRotation). The buffer keeps the physical orientation and holds
  // the content rotated; frames are rotated back so they are upright.
  void set_rotation(int degrees) { rotation_ = degrees; }

 private:
  void Loop();
  bool StreamFrames(int fd);
  std::shared_ptr<cuttlefish::VideoFrameBuffer> ReadFrame(
      int buffer_fd, const HwcFrameRecord& record);

  std::string socket_path_;
  int width_;
  int height_;
  FrameCallback on_frame_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> connected_{false};
  std::atomic<int> socket_fd_{-1};
  std::atomic<int> rotation_{0};
  std::thread thread_;
};

}  // namespace redroid
