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

#include "cuttlefish/host/frontend/redroid_webrtc/scrcpy_protocol.h"

#include <algorithm>
#include <cstring>

namespace redroid {
namespace scrcpy {
namespace {

constexpr uint8_t kTypeInjectKeycode = 0;
constexpr uint8_t kTypeInjectTouchEvent = 2;
constexpr uint8_t kTypeInjectScrollEvent = 3;
constexpr uint8_t kTypeBackOrScreenOn = 4;
constexpr uint8_t kTypeSetClipboard = 9;
constexpr uint8_t kTypeResetVideo = 17;

constexpr uint64_t kPacketFlagConfig = 1ULL << 63;
constexpr uint64_t kPacketFlagKeyFrame = 1ULL << 62;

void PutU16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(v >> 8);
  out.push_back(v & 0xff);
}

void PutU32(std::vector<uint8_t>& out, uint32_t v) {
  for (int shift = 24; shift >= 0; shift -= 8) {
    out.push_back((v >> shift) & 0xff);
  }
}

void PutU64(std::vector<uint8_t>& out, uint64_t v) {
  for (int shift = 56; shift >= 0; shift -= 8) {
    out.push_back((v >> shift) & 0xff);
  }
}

void PutPosition(std::vector<uint8_t>& out, int32_t x, int32_t y,
                 uint16_t width, uint16_t height) {
  PutU32(out, static_cast<uint32_t>(x));
  PutU32(out, static_cast<uint32_t>(y));
  PutU16(out, width);
  PutU16(out, height);
}

// Matches sc_float_to_u16fp() in scrcpy's app/src/util/binary.h.
uint16_t FloatToU16FixedPoint(float f) {
  f = std::clamp(f, 0.0f, 1.0f);
  uint32_t u = static_cast<uint32_t>(f * 0x1p16f);
  return u >= 0xffff ? 0xffff : static_cast<uint16_t>(u);
}

// Matches sc_float_to_i16fp().
int16_t FloatToI16FixedPoint(float f) {
  f = std::clamp(f, -1.0f, 1.0f);
  int32_t i = static_cast<int32_t>(f * 0x1p15f);
  return static_cast<int16_t>(std::clamp(i, -0x8000, 0x7fff));
}

uint64_t ParseU64(const uint8_t* data) {
  uint64_t v = 0;
  for (int i = 0; i < 8; i++) {
    v = (v << 8) | data[i];
  }
  return v;
}

}  // namespace

uint32_t ParseU32(const uint8_t data[4]) {
  return (uint32_t{data[0]} << 24) | (uint32_t{data[1]} << 16) |
         (uint32_t{data[2]} << 8) | data[3];
}

PacketHeader ParsePacketHeader(const uint8_t header[kPacketHeaderLength]) {
  uint64_t pts_and_flags = ParseU64(header);
  PacketHeader result;
  result.config = pts_and_flags & kPacketFlagConfig;
  result.key_frame = pts_and_flags & kPacketFlagKeyFrame;
  result.pts_us =
      result.config
          ? -1
          : static_cast<int64_t>(pts_and_flags &
                                 ~(kPacketFlagConfig | kPacketFlagKeyFrame));
  result.size = ParseU32(header + 8);
  return result;
}

VideoHeader ParseVideoHeader(const uint8_t header[kVideoHeaderLength]) {
  return VideoHeader{
      .codec = ParseU32(header),
      .width = ParseU32(header + 4),
      .height = ParseU32(header + 8),
  };
}

std::string ParseDeviceName(const uint8_t data[kDeviceNameLength]) {
  const char* chars = reinterpret_cast<const char*>(data);
  return std::string(chars, strnlen(chars, kDeviceNameLength));
}

std::vector<uint8_t> InjectKeycode(uint8_t action, int32_t keycode,
                                   int32_t repeat, int32_t meta_state) {
  std::vector<uint8_t> out = {kTypeInjectKeycode, action};
  PutU32(out, static_cast<uint32_t>(keycode));
  PutU32(out, static_cast<uint32_t>(repeat));
  PutU32(out, static_cast<uint32_t>(meta_state));
  return out;
}

std::vector<uint8_t> InjectTouch(uint8_t action, int64_t pointer_id, int32_t x,
                                 int32_t y, uint16_t screen_width,
                                 uint16_t screen_height, float pressure,
                                 int32_t action_button, int32_t buttons) {
  std::vector<uint8_t> out = {kTypeInjectTouchEvent, action};
  PutU64(out, static_cast<uint64_t>(pointer_id));
  PutPosition(out, x, y, screen_width, screen_height);
  PutU16(out, FloatToU16FixedPoint(pressure));
  PutU32(out, static_cast<uint32_t>(action_button));
  PutU32(out, static_cast<uint32_t>(buttons));
  return out;
}

std::vector<uint8_t> InjectScroll(int32_t x, int32_t y, uint16_t screen_width,
                                  uint16_t screen_height, float hscroll,
                                  float vscroll, int32_t buttons) {
  std::vector<uint8_t> out = {kTypeInjectScrollEvent};
  PutPosition(out, x, y, screen_width, screen_height);
  // The server decodes [-1, 1] and multiplies by 16.
  PutU16(out, static_cast<uint16_t>(FloatToI16FixedPoint(hscroll / 16)));
  PutU16(out, static_cast<uint16_t>(FloatToI16FixedPoint(vscroll / 16)));
  PutU32(out, static_cast<uint32_t>(buttons));
  return out;
}

std::vector<uint8_t> BackOrScreenOn(uint8_t action) {
  return {kTypeBackOrScreenOn, action};
}

std::vector<uint8_t> SetClipboard(int64_t sequence, const std::string& text,
                                  bool paste) {
  std::vector<uint8_t> out = {kTypeSetClipboard};
  PutU64(out, static_cast<uint64_t>(sequence));
  out.push_back(paste ? 1 : 0);
  PutU32(out, static_cast<uint32_t>(text.size()));
  out.insert(out.end(), text.begin(), text.end());
  return out;
}

std::vector<uint8_t> ResetVideo() { return {kTypeResetVideo}; }

}  // namespace scrcpy
}  // namespace redroid
