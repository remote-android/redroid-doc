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

#include <stddef.h>
#include <stdint.h>

#include <functional>
#include <memory>
#include <vector>

#include "cuttlefish/host/libs/screen_connector/video_frame_buffer.h"

struct AVCodecContext;
struct AVFrame;
struct AVPacket;

namespace redroid {

// Decodes an H.264 elementary stream (as sent by scrcpy) into planar I420
// frames (DRM_FORMAT_YUV420, needs patches/0003 in the cuttlefish streamer).
class H264Decoder {
 public:
  using FrameCallback =
      std::function<void(std::shared_ptr<cuttlefish::VideoFrameBuffer>)>;

  static std::unique_ptr<H264Decoder> Create(FrameCallback on_frame);
  ~H264Decoder();

  // Feeds one scrcpy packet. Config packets (SPS/PPS) are kept and prepended
  // to the next media packet, as the scrcpy client does.
  bool Decode(const uint8_t* data, size_t size, bool config);

 private:
  explicit H264Decoder(FrameCallback on_frame);

  FrameCallback on_frame_;
  AVCodecContext* context_ = nullptr;
  AVFrame* frame_ = nullptr;
  AVPacket* packet_ = nullptr;
  std::vector<uint8_t> pending_config_;
  std::vector<uint8_t> merged_;
};

}  // namespace redroid
