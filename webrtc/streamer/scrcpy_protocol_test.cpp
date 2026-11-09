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

#include <gtest/gtest.h>

#include <cstring>

namespace redroid::scrcpy {
namespace {

// Expected bytes follow scrcpy's app/tests/test_control_msg_serialize.c.

TEST(ScrcpyProtocolTest, InjectKeycode) {
  auto msg = InjectKeycode(kActionUp, /*KEYCODE_ENTER=*/66, /*repeat=*/5,
                           /*META_SHIFT_ON|META_SHIFT_LEFT_ON=*/0x41);
  std::vector<uint8_t> expected = {
      0x00,                    // TYPE_INJECT_KEYCODE
      0x01,                    // AKEY_EVENT_ACTION_UP
      0x00, 0x00, 0x00, 0x42,  // keycode
      0x00, 0x00, 0x00, 0x05,  // repeat
      0x00, 0x00, 0x00, 0x41,  // meta state
  };
  EXPECT_EQ(msg, expected);
}

TEST(ScrcpyProtocolTest, InjectTouch) {
  auto msg = InjectTouch(kActionDown, /*pointer_id=*/0x1234567887654321LL, 100,
                         200, 1080, 1920, 1.0f, /*action_button=*/1,
                         /*buttons=*/1);
  std::vector<uint8_t> expected = {
      0x02,  // TYPE_INJECT_TOUCH_EVENT
      0x00,  // ACTION_DOWN
      0x12, 0x34, 0x56, 0x78, 0x87, 0x65, 0x43, 0x21,  // pointer id
      0x00, 0x00, 0x00, 0x64,                          // x = 100
      0x00, 0x00, 0x00, 0xc8,                          // y = 200
      0x04, 0x38, 0x07, 0x80,                          // 1080x1920
      0xff, 0xff,                                      // pressure 1.0
      0x00, 0x00, 0x00, 0x01,                          // action button
      0x00, 0x00, 0x00, 0x01,                          // buttons
  };
  EXPECT_EQ(msg, expected);
}

TEST(ScrcpyProtocolTest, InjectTouchNegativePointerIdAndZeroPressure) {
  auto msg = InjectTouch(kActionUp, kPointerIdGenericFinger, 0, 0, 720, 1280,
                         0.0f, 0, 0);
  ASSERT_EQ(msg.size(), 32u);
  std::vector<uint8_t> pointer(msg.begin() + 2, msg.begin() + 10);
  EXPECT_EQ(pointer, std::vector<uint8_t>(
                         {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe}));
  EXPECT_EQ(msg[22], 0x00);
  EXPECT_EQ(msg[23], 0x00);
}

TEST(ScrcpyProtocolTest, InjectScroll) {
  auto msg = InjectScroll(260, 1026, 1080, 1920, /*hscroll=*/16.0f,
                          /*vscroll=*/-16.0f, /*buttons=*/1);
  std::vector<uint8_t> expected = {
      0x03,                    // TYPE_INJECT_SCROLL_EVENT
      0x00, 0x00, 0x01, 0x04,  // x = 260
      0x00, 0x00, 0x04, 0x02,  // y = 1026
      0x04, 0x38, 0x07, 0x80,  // 1080x1920
      0x7f, 0xff,              // 16 (=1.0 in i16 fixed point)
      0x80, 0x00,              // -16 (=-1.0)
      0x00, 0x00, 0x00, 0x01,  // buttons
  };
  EXPECT_EQ(msg, expected);
}

TEST(ScrcpyProtocolTest, BackOrScreenOnAndResetVideo) {
  EXPECT_EQ(BackOrScreenOn(kActionDown), std::vector<uint8_t>({0x04, 0x00}));
  EXPECT_EQ(ResetVideo(), std::vector<uint8_t>({0x11}));
}

TEST(ScrcpyProtocolTest, SetClipboard) {
  auto msg = SetClipboard(0x5555555555555555LL, "hello", /*paste=*/true);
  std::vector<uint8_t> expected = {
      0x09,                                            // TYPE_SET_CLIPBOARD
      0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,  // sequence
      0x01,                                            // paste
      0x00, 0x00, 0x00, 0x05,                          // length
      'h',  'e',  'l',  'l',  'o',
  };
  EXPECT_EQ(msg, expected);
}

TEST(ScrcpyProtocolTest, ParsePacketHeader) {
  const uint8_t config[12] = {0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x00};
  auto header = ParsePacketHeader(config);
  EXPECT_TRUE(header.config);
  EXPECT_FALSE(header.key_frame);
  EXPECT_EQ(header.pts_us, -1);
  EXPECT_EQ(header.size, 256u);

  const uint8_t key[12] = {0x40, 0,    0, 0, 0,    0x01,
                           0xe2, 0x40, 0, 0, 0x10, 0x00};
  header = ParsePacketHeader(key);
  EXPECT_FALSE(header.config);
  EXPECT_TRUE(header.key_frame);
  EXPECT_EQ(header.pts_us, 123456);
  EXPECT_EQ(header.size, 4096u);
}

TEST(ScrcpyProtocolTest, ParseHeaders) {
  const uint8_t video[12] = {'h',  '2',  '6', '4', 0,    0,
                             0x02, 0xd0, 0,   0,   0x05, 0x00};
  auto header = ParseVideoHeader(video);
  EXPECT_EQ(header.codec, kCodecH264);
  EXPECT_EQ(header.width, 720u);
  EXPECT_EQ(header.height, 1280u);

  uint8_t name[kDeviceNameLength] = {};
  memcpy(name, "redroid14", 9);
  EXPECT_EQ(ParseDeviceName(name), "redroid14");

  const uint8_t raw[4] = {0x00, 'r', 'a', 'w'};
  EXPECT_EQ(ParseU32(raw), kCodecRaw);
}

}  // namespace
}  // namespace redroid::scrcpy
