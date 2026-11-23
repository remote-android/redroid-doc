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

// H.264 passthrough: the device's H.264 stream (from scrcpy) is sent to the
// browsers as is, without decoding and re-encoding.
//
// Encoded access units travel through the cuttlefish streamer as video frames
// of a pseudo pixel format (EncodedH264Frame). A passthrough encoder,
// registered as the highest priority H.264 encoder provider, unwraps them
// (patches/0004) and hands them to WebRTC. Encoders cannot ask the device for
// keyframes directly, so they call a keyframe requester (scrcpy RESET_VIDEO).

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "cuttlefish/host/libs/screen_connector/video_frame_buffer.h"

namespace redroid {

// fourcc('H', '2', '6', '4'), not a real DRM format.
constexpr uint32_t kEncodedH264Format = 0x34363248;

// One H.264 access unit (Annex B), as a "video frame".
class EncodedH264Frame : public cuttlefish::VideoFrameBuffer {
 public:
  EncodedH264Frame(int width, int height, bool key_frame, uint64_t sequence,
                   std::vector<uint8_t> data);

  int width() const override { return width_; }
  int height() const override { return height_; }
  uint8_t* Data() const override { return const_cast<uint8_t*>(data_.data()); }
  int Stride() const override { return 0; }
  size_t DataSize() const override { return data_.size(); }
  uint32_t PixelFormat() const override { return kEncodedH264Format; }
  std::unique_ptr<VideoFrameBuffer> Clone() const override;

  bool key_frame() const { return key_frame_; }
  // Consecutive for consecutive access units of one stream, so encoders can
  // detect frames that WebRTC dropped before encoding.
  uint64_t sequence() const { return sequence_; }

 private:
  int width_;
  int height_;
  bool key_frame_;
  uint64_t sequence_;
  std::vector<uint8_t> data_;
};

// Turns scrcpy video packets into EncodedH264Frames: keeps the latest SPS/PPS
// and prepends them to every key frame (so any client can start decoding
// there) and tracks the picture size from the SPS (it changes on rotation).
class H264FrameAssembler {
 public:
  using FrameCallback = std::function<void(std::shared_ptr<EncodedH264Frame>)>;

  explicit H264FrameAssembler(FrameCallback on_frame);

  void OnPacket(const uint8_t* data, size_t size, bool config, bool key_frame);

  int width() const { return width_; }
  int height() const { return height_; }

 private:
  FrameCallback on_frame_;
  std::vector<uint8_t> config_;
  int width_ = 0;
  int height_ = 0;
  uint64_t sequence_ = 0;
};

// Picture size from the SPS found in an Annex B buffer.
std::optional<std::pair<int, int>> H264PictureSize(const uint8_t* data,
                                                   size_t size);

// Registers the passthrough encoder provider. Must be called before the
// streamer is created. request_key_frame is called (rate limited) when a
// client needs a key frame.
void RegisterH264PassthroughEncoder(std::function<void()> request_key_frame);

}  // namespace redroid
