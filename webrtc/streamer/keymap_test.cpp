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

#include "cuttlefish/host/frontend/redroid_webrtc/keymap.h"

#include <gtest/gtest.h>

namespace redroid {
namespace {

TEST(KeymapTest, LettersDigitsAndControlKeys) {
  EXPECT_EQ(LinuxToAndroidKeycode(30), 29);   // KEY_A -> KEYCODE_A
  EXPECT_EQ(LinuxToAndroidKeycode(44), 54);   // KEY_Z -> KEYCODE_Z
  EXPECT_EQ(LinuxToAndroidKeycode(11), 7);    // KEY_0 -> KEYCODE_0
  EXPECT_EQ(LinuxToAndroidKeycode(28), 66);   // KEY_ENTER -> KEYCODE_ENTER
  EXPECT_EQ(LinuxToAndroidKeycode(14), 67);   // KEY_BACKSPACE -> KEYCODE_DEL
  EXPECT_EQ(LinuxToAndroidKeycode(103), 19);  // KEY_UP -> KEYCODE_DPAD_UP
  EXPECT_EQ(LinuxToAndroidKeycode(116), kAndroidKeycodePower);
  EXPECT_EQ(LinuxToAndroidKeycode(158), kAndroidKeycodeBack);
  EXPECT_EQ(LinuxToAndroidKeycode(0), std::nullopt);
  EXPECT_EQ(LinuxToAndroidKeycode(240), std::nullopt);
}

TEST(KeymapTest, MetaStateTracksBothSides) {
  MetaState meta;
  constexpr int32_t kShiftLeft = 59, kShiftRight = 60, kCtrlLeft = 113;
  EXPECT_EQ(meta.Update(kShiftLeft, true), 0x41);
  EXPECT_EQ(meta.Update(kShiftRight, true), 0xc1);
  EXPECT_EQ(meta.Update(29, true), 0xc1);  // 'a' keeps the state
  EXPECT_EQ(meta.Update(kShiftLeft, false), 0x81);
  EXPECT_EQ(meta.Update(kShiftRight, false), 0x0);
  EXPECT_EQ(meta.Update(kCtrlLeft, true), 0x3000);
  EXPECT_EQ(meta.Update(kCtrlLeft, false), 0x0);
}

}  // namespace
}  // namespace redroid
