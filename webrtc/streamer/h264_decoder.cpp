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

#include "cuttlefish/host/frontend/redroid_webrtc/h264_decoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

#include <libyuv.h>

#include <cstring>
#include <memory>

#include "absl/log/log.h"
#include "drm/drm_fourcc.h"

namespace redroid {
namespace {

// A planar I420 frame owning its pixels: Y (stride = width), then U and V
// (stride = (width + 1) / 2), contiguous. The cuttlefish streamer passes it
// to the encoder without conversion (patches/0003). Not zero initialized: the
// copy writes every byte.
class I420Frame : public cuttlefish::VideoFrameBuffer {
 public:
  I420Frame(int width, int height)
      : width_(width),
        height_(height),
        data_(std::make_unique_for_overwrite<uint8_t[]>(DataSize())) {}

  int width() const override { return width_; }
  int height() const override { return height_; }
  uint8_t* Data() const override { return data_.get(); }
  int Stride() const override { return width_; }
  int ChromaStride() const { return (width_ + 1) / 2; }
  uint8_t* DataU() const { return data_.get() + width_ * height_; }
  uint8_t* DataV() const {
    return DataU() + ChromaStride() * ((height_ + 1) / 2);
  }
  size_t DataSize() const override {
    return static_cast<size_t>(width_) * height_ +
           2 * static_cast<size_t>(ChromaStride()) * ((height_ + 1) / 2);
  }
  uint32_t PixelFormat() const override { return DRM_FORMAT_YUV420; }
  std::unique_ptr<VideoFrameBuffer> Clone() const override {
    auto clone = std::make_unique<I420Frame>(width_, height_);
    memcpy(clone->Data(), Data(), DataSize());
    return clone;
  }

 private:
  int width_;
  int height_;
  std::unique_ptr<uint8_t[]> data_;
};

}  // namespace

std::unique_ptr<H264Decoder> H264Decoder::Create(FrameCallback on_frame) {
  std::unique_ptr<H264Decoder> decoder(new H264Decoder(std::move(on_frame)));
  const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
  if (!codec) {
    LOG(ERROR) << "libavcodec has no H.264 decoder";
    return nullptr;
  }
  decoder->context_ = avcodec_alloc_context3(codec);
  decoder->frame_ = av_frame_alloc();
  decoder->packet_ = av_packet_alloc();
  if (!decoder->context_ || !decoder->frame_ || !decoder->packet_) {
    return nullptr;
  }
  // Lowest latency: no frame reordering delay, slice threads only.
  decoder->context_->flags |= AV_CODEC_FLAG_LOW_DELAY;
  decoder->context_->thread_type = FF_THREAD_SLICE;
  if (avcodec_open2(decoder->context_, codec, nullptr) < 0) {
    LOG(ERROR) << "Failed to open the H.264 decoder";
    return nullptr;
  }
  return decoder;
}

H264Decoder::H264Decoder(FrameCallback on_frame)
    : on_frame_(std::move(on_frame)) {}

H264Decoder::~H264Decoder() {
  av_packet_free(&packet_);
  av_frame_free(&frame_);
  avcodec_free_context(&context_);
}

bool H264Decoder::Decode(const uint8_t* data, size_t size, bool config) {
  if (config) {
    pending_config_.assign(data, data + size);
    return true;
  }
  if (!pending_config_.empty()) {
    merged_ = std::move(pending_config_);
    pending_config_.clear();
    merged_.insert(merged_.end(), data, data + size);
    data = merged_.data();
    size = merged_.size();
  }
  packet_->data = const_cast<uint8_t*>(data);
  packet_->size = static_cast<int>(size);
  int ret = avcodec_send_packet(context_, packet_);
  if (ret < 0) {
    LOG(WARNING) << "avcodec_send_packet failed: " << ret;
    return false;
  }
  while ((ret = avcodec_receive_frame(context_, frame_)) == 0) {
    if (frame_->format != AV_PIX_FMT_YUV420P &&
        frame_->format != AV_PIX_FMT_YUVJ420P) {
      LOG(ERROR) << "Unexpected decoded pixel format " << frame_->format;
      av_frame_unref(frame_);
      return false;
    }
    auto out = std::make_shared<I420Frame>(frame_->width, frame_->height);
    libyuv::I420Copy(frame_->data[0], frame_->linesize[0], frame_->data[1],
                     frame_->linesize[1], frame_->data[2], frame_->linesize[2],
                     out->Data(), out->Stride(), out->DataU(),
                     out->ChromaStride(), out->DataV(), out->ChromaStride(),
                     frame_->width, frame_->height);
    av_frame_unref(frame_);
    on_frame_(std::move(out));
  }
  return ret == AVERROR(EAGAIN) || ret == AVERROR_EOF;
}

}  // namespace redroid
