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

#include "cuttlefish/host/frontend/redroid_webrtc/h264_passthrough.h"

#include <atomic>
#include <chrono>
#include <limits>
#include <string>

#include "absl/log/log.h"
#include "api/video/encoded_image.h"
#include "api/video/video_frame.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_decoder.h"
#include "api/video_codecs/video_encoder.h"
#include "common_video/h264/h264_common.h"
#include "common_video/h264/sps_parser.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "modules/video_coding/include/video_error_codes.h"

#include "cuttlefish/host/frontend/webrtc/libcommon/abgr_buffer.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/decoder_provider.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/decoder_provider_registry.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/encoder_provider.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/encoder_provider_registry.h"

namespace redroid {

EncodedH264Frame::EncodedH264Frame(int width, int height, bool key_frame,
                                   uint64_t sequence, std::vector<uint8_t> data)
    : width_(width),
      height_(height),
      key_frame_(key_frame),
      sequence_(sequence),
      data_(std::move(data)) {}

std::unique_ptr<cuttlefish::VideoFrameBuffer> EncodedH264Frame::Clone() const {
  return std::make_unique<EncodedH264Frame>(*this);
}

std::optional<std::pair<int, int>> H264PictureSize(const uint8_t* data,
                                                   size_t size) {
  for (const auto& nalu : webrtc::H264::FindNaluIndices(data, size)) {
    if (nalu.payload_size < 2) {
      continue;
    }
    const uint8_t* payload = data + nalu.payload_start_offset;
    if (webrtc::H264::ParseNaluType(payload[0]) != webrtc::H264::kSps) {
      continue;
    }
    // ParseSps expects the payload after the NAL unit header byte.
    auto sps = webrtc::SpsParser::ParseSps(payload + 1, nalu.payload_size - 1);
    if (sps) {
      return std::make_pair(static_cast<int>(sps->width),
                            static_cast<int>(sps->height));
    }
  }
  return std::nullopt;
}

H264FrameAssembler::H264FrameAssembler(FrameCallback on_frame)
    : on_frame_(std::move(on_frame)) {}

void H264FrameAssembler::OnPacket(const uint8_t* data, size_t size, bool config,
                                  bool key_frame) {
  if (config) {
    config_.assign(data, data + size);
    if (auto picture = H264PictureSize(data, size)) {
      if (picture->first != width_ || picture->second != height_) {
        LOG(INFO) << "H.264 stream is " << picture->first << "x"
                  << picture->second;
      }
      width_ = picture->first;
      height_ = picture->second;
    }
    return;
  }
  if (width_ == 0 || height_ == 0) {
    return;  // No SPS seen yet, nothing can decode this.
  }
  std::vector<uint8_t> frame;
  if (key_frame) {
    frame.reserve(config_.size() + size);
    frame.insert(frame.end(), config_.begin(), config_.end());
  }
  frame.insert(frame.end(), data, data + size);
  on_frame_(std::make_shared<EncodedH264Frame>(width_, height_, key_frame,
                                               ++sequence_, std::move(frame)));
}

namespace {

using cuttlefish::Result;

constexpr auto kKeyFrameRequestInterval = std::chrono::milliseconds(500);

class KeyFrameRequester {
 public:
  explicit KeyFrameRequester(std::function<void()> request)
      : request_(std::move(request)) {}

  void Request() {
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    auto last = last_.load();
    auto interval =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            kKeyFrameRequestInterval)
            .count();
    if (now - last < interval || !last_.compare_exchange_strong(last, now)) {
      return;
    }
    VLOG(1) << "Requesting a key frame from the device";
    request_();
  }

 private:
  std::function<void()> request_;
  std::atomic<std::chrono::steady_clock::rep> last_{0};
};

// Android's encoders produce baseline/main/high profile streams;
// level-asymmetry-allowed lets the browser accept any level.
std::vector<webrtc::SdpVideoFormat> H264Formats() {
  std::vector<webrtc::SdpVideoFormat> formats;
  for (const char* profile : {"42e01f", "42001f", "4d001f", "640c1f"}) {
    formats.emplace_back("H264", webrtc::SdpVideoFormat::Parameters{
                                     {"profile-level-id", profile},
                                     {"level-asymmetry-allowed", "1"},
                                     {"packetization-mode", "1"},
                                 });
  }
  return formats;
}

// One per WebRTC video sender (i.e. per client).
class PassthroughEncoder : public webrtc::VideoEncoder {
 public:
  explicit PassthroughEncoder(std::shared_ptr<KeyFrameRequester> requester)
      : requester_(std::move(requester)) {}

  int InitEncode(const webrtc::VideoCodec*,
                 const webrtc::VideoEncoder::Settings&) override {
    return WEBRTC_VIDEO_CODEC_OK;
  }

  int32_t RegisterEncodeCompleteCallback(
      webrtc::EncodedImageCallback* callback) override {
    callback_ = callback;
    return WEBRTC_VIDEO_CODEC_OK;
  }

  int32_t Release() override {
    callback_ = nullptr;
    return WEBRTC_VIDEO_CODEC_OK;
  }

  void SetRates(const RateControlParameters&) override {
    // The device encoder's bit rate is fixed (--video_bit_rate).
  }

  EncoderInfo GetEncoderInfo() const override {
    EncoderInfo info;
    // Receive the native frames, not I420 conversions of them.
    info.supports_native_handle = true;
    // Don't drop frames to meet the target bit rate: a dropped frame
    // corrupts every following frame until the next key frame.
    info.has_trusted_rate_controller = true;
    info.is_hardware_accelerated = true;
    info.scaling_settings = ScalingSettings::kOff;
    info.implementation_name = "redroid_h264_passthrough";
    return info;
  }

  int32_t Encode(
      const webrtc::VideoFrame& frame,
      const std::vector<webrtc::VideoFrameType>* frame_types) override {
    if (!callback_) {
      return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
    }
    auto buffer = frame.video_frame_buffer();
    if (buffer->type() != webrtc::VideoFrameBuffer::Type::kNative) {
      LOG(ERROR) << "Passthrough encoder got a non native frame";
      return WEBRTC_VIDEO_CODEC_ERROR;
    }
    // Native frames from the cuttlefish streamer are always AbgrBuffers.
    const auto& wrapped =
        static_cast<cuttlefish::AbgrBuffer*>(buffer.get())->buffer();
    if (wrapped->PixelFormat() != kEncodedH264Format) {
      LOG(ERROR) << "Passthrough encoder got a raw frame, is the video source "
                    "in passthrough mode?";
      return WEBRTC_VIDEO_CODEC_ERROR;
    }
    const auto& au = static_cast<const EncodedH264Frame&>(*wrapped);

    bool key_requested = false;
    if (frame_types) {
      for (auto type : *frame_types) {
        key_requested |= type == webrtc::VideoFrameType::kVideoFrameKey;
      }
    }
    bool gap = au.sequence() != last_sequence_ + 1;
    if (!au.key_frame() && (key_requested || gap)) {
      waiting_for_key_frame_ = true;
    }
    last_sequence_ = au.sequence();
    if (waiting_for_key_frame_ && !au.key_frame()) {
      requester_->Request();
      return WEBRTC_VIDEO_CODEC_OK;  // Undecodable without a key frame.
    }
    waiting_for_key_frame_ = false;

    webrtc::EncodedImage image;
    image.SetEncodedData(
        webrtc::EncodedImageBuffer::Create(au.Data(), au.DataSize()));
    image._encodedWidth = au.width();
    image._encodedHeight = au.height();
    image.SetTimestamp(frame.timestamp());
    image.capture_time_ms_ = frame.render_time_ms();
    image._frameType = au.key_frame()
                           ? webrtc::VideoFrameType::kVideoFrameKey
                           : webrtc::VideoFrameType::kVideoFrameDelta;
    image.rotation_ = frame.rotation();

    webrtc::CodecSpecificInfo info;
    info.codecType = webrtc::kVideoCodecH264;
    info.codecSpecific.H264.packetization_mode =
        webrtc::H264PacketizationMode::NonInterleaved;
    info.codecSpecific.H264.idr_frame = au.key_frame();
    callback_->OnEncodedImage(image, &info);
    return WEBRTC_VIDEO_CODEC_OK;
  }

 private:
  std::shared_ptr<KeyFrameRequester> requester_;
  webrtc::EncodedImageCallback* callback_ = nullptr;
  uint64_t last_sequence_ = std::numeric_limits<uint64_t>::max() - 1;
  bool waiting_for_key_frame_ = true;
};

class PassthroughEncoderProvider
    : public cuttlefish::webrtc_streaming::EncoderProvider {
 public:
  explicit PassthroughEncoderProvider(
      std::shared_ptr<KeyFrameRequester> requester)
      : requester_(std::move(requester)) {}

  std::string GetName() const override { return "redroid_h264_passthrough"; }
  // Above hardware encoders (100+): in passthrough mode no other encoder can
  // handle the frames.
  int GetPriority() const override { return 1000; }
  bool IsAvailable() const override { return true; }

  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
    return H264Formats();
  }

  Result<std::unique_ptr<webrtc::VideoEncoder>> CreateEncoder(
      const webrtc::SdpVideoFormat& format) const override {
    using namespace cuttlefish;  // CF_EXPECT's helpers
    CF_EXPECT(format.name == "H264",
              "Passthrough encoder only supports H264, not " << format.name);
    return std::unique_ptr<webrtc::VideoEncoder>(
        std::make_unique<PassthroughEncoder>(requester_));
  }

 private:
  std::shared_ptr<KeyFrameRequester> requester_;
};

// libwebrtc only offers codecs that it can both send and receive on a
// send-receive video section, and there is no H.264 decoder in this build.
// This one makes H.264 negotiable; nothing ever sends H.264 video to the
// device (the browser camera is not supported), so it only drops input.
class NullH264Decoder : public webrtc::VideoDecoder {
 public:
  bool Configure(const Settings&) override { return true; }
  int32_t Decode(const webrtc::EncodedImage&, bool, int64_t) override {
    return WEBRTC_VIDEO_CODEC_ERROR;
  }
  int32_t RegisterDecodeCompleteCallback(
      webrtc::DecodedImageCallback*) override {
    return WEBRTC_VIDEO_CODEC_OK;
  }
  int32_t Release() override { return WEBRTC_VIDEO_CODEC_OK; }
};

class NullH264DecoderProvider
    : public cuttlefish::webrtc_streaming::DecoderProvider {
 public:
  std::string GetName() const override { return "redroid_null_h264"; }
  int GetPriority() const override { return 0; }
  bool IsAvailable() const override { return true; }
  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
    return H264Formats();
  }
  Result<std::unique_ptr<webrtc::VideoDecoder>> CreateDecoder(
      const webrtc::SdpVideoFormat& format) const override {
    using namespace cuttlefish;  // CF_EXPECT's helpers
    CF_EXPECT(format.name == "H264", "Not H264: " << format.name);
    return std::unique_ptr<webrtc::VideoDecoder>(
        std::make_unique<NullH264Decoder>());
  }
};

}  // namespace

void RegisterH264PassthroughEncoder(std::function<void()> request_key_frame) {
  auto& registry = cuttlefish::webrtc_streaming::EncoderProviderRegistry::Get();
  // Only the passthrough encoder can handle encoded frames: no other codec
  // may be negotiated (patches/0005).
  std::vector<std::string> others;
  for (const auto* provider : registry.GetProviders()) {
    others.push_back(provider->GetName());
  }
  for (const auto& name : others) {
    registry.Unregister(name);
  }
  cuttlefish::webrtc_streaming::DecoderProviderRegistry::Get().Register(
      std::make_unique<NullH264DecoderProvider>());
  registry.Register(std::make_unique<PassthroughEncoderProvider>(
      std::make_shared<KeyFrameRequester>(std::move(request_key_frame))));
}

}  // namespace redroid
