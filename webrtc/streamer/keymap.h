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

#include <stdint.h>

#include <optional>

namespace redroid {

// Android KeyEvent keycodes used outside the keymap.
constexpr int32_t kAndroidKeycodeHome = 3;
constexpr int32_t kAndroidKeycodeBack = 4;
constexpr int32_t kAndroidKeycodeVolumeUp = 24;
constexpr int32_t kAndroidKeycodeVolumeDown = 25;
constexpr int32_t kAndroidKeycodePower = 26;
constexpr int32_t kAndroidKeycodeMenu = 82;
constexpr int32_t kAndroidKeycodeAppSwitch = 187;

// Maps a Linux input event keycode (as sent by the cuttlefish html client) to
// an Android KeyEvent keycode, following AOSP's Generic.kl.
std::optional<int32_t> LinuxToAndroidKeycode(uint16_t linux_keycode);

// Tracks modifier keys to compute the Android KeyEvent meta state.
class MetaState {
 public:
  // Updates the state with a key event, returns the meta state to use for it.
  int32_t Update(int32_t android_keycode, bool down);
  int32_t value() const { return state_; }

 private:
  int32_t state_ = 0;
};

}  // namespace redroid
