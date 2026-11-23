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

// Wire format of the scrcpy server protocol (scrcpy v3.x), see
// server/src/main/java/com/genymobile/scrcpy/control/ControlMessageReader.java
// and server/src/main/java/com/genymobile/scrcpy/device/Streamer.java.
// All integers are big endian.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <string>
#include <vector>

namespace redroid {
namespace scrcpy {

// Codec ids sent in the stream headers ("h264", "opus", "raw " as fourcc).
constexpr uint32_t kCodecH264 = 0x68323634;
constexpr uint32_t kCodecH265 = 0x68323635;
constexpr uint32_t kCodecAv1 = 0x00617631;
constexpr uint32_t kCodecRaw = 0x00726177;
constexpr uint32_t kCodecOpus = 0x6f707573;
// A stream header containing these values instead of a codec id means the
// stream was disabled by the device (or failed to start).
constexpr uint32_t kStreamDisabled = 0;
constexpr uint32_t kStreamError = 1;

constexpr size_t kDeviceNameLength = 64;
constexpr size_t kPacketHeaderLength = 12;
constexpr size_t kVideoHeaderLength = 12;

// Android constants used in control messages.
constexpr uint8_t kActionDown = 0;  // MotionEvent / KeyEvent ACTION_DOWN
constexpr uint8_t kActionUp = 1;    // MotionEvent / KeyEvent ACTION_UP
constexpr uint8_t kActionMove = 2;  // MotionEvent ACTION_MOVE
constexpr int64_t kPointerIdMouse = -1;
constexpr int64_t kPointerIdGenericFinger = -2;

struct PacketHeader {
  bool config;     // codec config data (e.g. SPS/PPS), no pts
  bool key_frame;  // only meaningful for video
  int64_t pts_us;  // presentation timestamp, -1 for config packets
  uint32_t size;   // payload size following the header
};

struct VideoHeader {
  uint32_t codec;
  uint32_t width;
  uint32_t height;
};

PacketHeader ParsePacketHeader(const uint8_t header[kPacketHeaderLength]);
VideoHeader ParseVideoHeader(const uint8_t header[kVideoHeaderLength]);
uint32_t ParseU32(const uint8_t data[4]);
std::string ParseDeviceName(const uint8_t data[kDeviceNameLength]);

// Control messages (client -> device).
std::vector<uint8_t> InjectKeycode(uint8_t action, int32_t keycode,
                                   int32_t repeat, int32_t meta_state);
std::vector<uint8_t> InjectTouch(uint8_t action, int64_t pointer_id, int32_t x,
                                 int32_t y, uint16_t screen_width,
                                 uint16_t screen_height, float pressure,
                                 int32_t action_button, int32_t buttons);
// hscroll/vscroll are in [-16, 16].
std::vector<uint8_t> InjectScroll(int32_t x, int32_t y, uint16_t screen_width,
                                  uint16_t screen_height, float hscroll,
                                  float vscroll, int32_t buttons);
std::vector<uint8_t> BackOrScreenOn(uint8_t action);
std::vector<uint8_t> SetClipboard(int64_t sequence, const std::string& text,
                                  bool paste);
std::vector<uint8_t> ResetVideo();

}  // namespace scrcpy
}  // namespace redroid
