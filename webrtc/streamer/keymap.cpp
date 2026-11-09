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

#include <unordered_map>

namespace redroid {
namespace {

// Linux keycode (include/uapi/linux/input-event-codes.h) -> Android keycode
// (frameworks/base/core/java/android/view/KeyEvent.java), from Generic.kl.
const std::unordered_map<uint16_t, int32_t>& Keymap() {
  static const auto* map = new std::unordered_map<uint16_t, int32_t>{
      {1, 111},    // ESC -> ESCAPE
      {2, 8},      // 1
      {3, 9},      // 2
      {4, 10},     // 3
      {5, 11},     // 4
      {6, 12},     // 5
      {7, 13},     // 6
      {8, 14},     // 7
      {9, 15},     // 8
      {10, 16},    // 9
      {11, 7},     // 0
      {12, 69},    // MINUS
      {13, 70},    // EQUAL -> EQUALS
      {14, 67},    // BACKSPACE -> DEL
      {15, 61},    // TAB
      {16, 45},    // Q
      {17, 51},    // W
      {18, 33},    // E
      {19, 46},    // R
      {20, 48},    // T
      {21, 53},    // Y
      {22, 49},    // U
      {23, 37},    // I
      {24, 43},    // O
      {25, 44},    // P
      {26, 71},    // LEFTBRACE -> LEFT_BRACKET
      {27, 72},    // RIGHTBRACE -> RIGHT_BRACKET
      {28, 66},    // ENTER
      {29, 113},   // LEFTCTRL -> CTRL_LEFT
      {30, 29},    // A
      {31, 47},    // S
      {32, 32},    // D
      {33, 34},    // F
      {34, 35},    // G
      {35, 36},    // H
      {36, 38},    // J
      {37, 39},    // K
      {38, 40},    // L
      {39, 74},    // SEMICOLON
      {40, 75},    // APOSTROPHE
      {41, 68},    // GRAVE
      {42, 59},    // LEFTSHIFT -> SHIFT_LEFT
      {43, 73},    // BACKSLASH
      {44, 54},    // Z
      {45, 52},    // X
      {46, 31},    // C
      {47, 50},    // V
      {48, 30},    // B
      {49, 42},    // N
      {50, 41},    // M
      {51, 55},    // COMMA
      {52, 56},    // DOT -> PERIOD
      {53, 76},    // SLASH
      {54, 60},    // RIGHTSHIFT -> SHIFT_RIGHT
      {55, 155},   // KPASTERISK -> NUMPAD_MULTIPLY
      {56, 57},    // LEFTALT -> ALT_LEFT
      {57, 62},    // SPACE
      {58, 115},   // CAPSLOCK -> CAPS_LOCK
      {59, 131},   // F1
      {60, 132},   // F2
      {61, 133},   // F3
      {62, 134},   // F4
      {63, 135},   // F5
      {64, 136},   // F6
      {65, 137},   // F7
      {66, 138},   // F8
      {67, 139},   // F9
      {68, 140},   // F10
      {69, 143},   // NUMLOCK -> NUM_LOCK
      {70, 116},   // SCROLLLOCK -> SCROLL_LOCK
      {71, 151},   // KP7
      {72, 152},   // KP8
      {73, 153},   // KP9
      {74, 156},   // KPMINUS -> NUMPAD_SUBTRACT
      {75, 148},   // KP4
      {76, 149},   // KP5
      {77, 150},   // KP6
      {78, 157},   // KPPLUS -> NUMPAD_ADD
      {79, 145},   // KP1
      {80, 146},   // KP2
      {81, 147},   // KP3
      {82, 144},   // KP0
      {83, 158},   // KPDOT -> NUMPAD_DOT
      {87, 141},   // F11
      {88, 142},   // F12
      {96, 160},   // KPENTER -> NUMPAD_ENTER
      {97, 114},   // RIGHTCTRL -> CTRL_RIGHT
      {98, 154},   // KPSLASH -> NUMPAD_DIVIDE
      {99, 120},   // SYSRQ
      {100, 58},   // RIGHTALT -> ALT_RIGHT
      {102, 122},  // HOME -> MOVE_HOME
      {103, 19},   // UP -> DPAD_UP
      {104, 92},   // PAGEUP -> PAGE_UP
      {105, 21},   // LEFT -> DPAD_LEFT
      {106, 22},   // RIGHT -> DPAD_RIGHT
      {107, 123},  // END -> MOVE_END
      {108, 20},   // DOWN -> DPAD_DOWN
      {109, 93},   // PAGEDOWN -> PAGE_DOWN
      {110, 124},  // INSERT
      {111, 112},  // DELETE -> FORWARD_DEL
      {113, 164},  // MUTE -> VOLUME_MUTE
      {114, kAndroidKeycodeVolumeDown},
      {115, kAndroidKeycodeVolumeUp},
      {116, kAndroidKeycodePower},
      {125, 117},                  // LEFTMETA -> META_LEFT
      {126, 118},                  // RIGHTMETA -> META_RIGHT
      {127, kAndroidKeycodeMenu},  // COMPOSE -> MENU
      {139, kAndroidKeycodeMenu},  // MENU
      {158, kAndroidKeycodeBack},  // BACK
      {172, kAndroidKeycodeHome},  // HOMEPAGE -> HOME
  };
  return *map;
}

// android.view.KeyEvent META_* flags.
constexpr int32_t kMetaShiftOn = 0x1;
constexpr int32_t kMetaAltOn = 0x2;
constexpr int32_t kMetaAltLeftOn = 0x10;
constexpr int32_t kMetaAltRightOn = 0x20;
constexpr int32_t kMetaShiftLeftOn = 0x40;
constexpr int32_t kMetaShiftRightOn = 0x80;
constexpr int32_t kMetaCtrlOn = 0x1000;
constexpr int32_t kMetaCtrlLeftOn = 0x2000;
constexpr int32_t kMetaCtrlRightOn = 0x4000;
constexpr int32_t kMetaMetaOn = 0x10000;
constexpr int32_t kMetaMetaLeftOn = 0x20000;
constexpr int32_t kMetaMetaRightOn = 0x40000;

int32_t ModifierFlags(int32_t android_keycode) {
  switch (android_keycode) {
    case 59:
      return kMetaShiftOn | kMetaShiftLeftOn;
    case 60:
      return kMetaShiftOn | kMetaShiftRightOn;
    case 57:
      return kMetaAltOn | kMetaAltLeftOn;
    case 58:
      return kMetaAltOn | kMetaAltRightOn;
    case 113:
      return kMetaCtrlOn | kMetaCtrlLeftOn;
    case 114:
      return kMetaCtrlOn | kMetaCtrlRightOn;
    case 117:
      return kMetaMetaOn | kMetaMetaLeftOn;
    case 118:
      return kMetaMetaOn | kMetaMetaRightOn;
    default:
      return 0;
  }
}

}  // namespace

std::optional<int32_t> LinuxToAndroidKeycode(uint16_t linux_keycode) {
  auto it = Keymap().find(linux_keycode);
  if (it == Keymap().end()) {
    return std::nullopt;
  }
  return it->second;
}

int32_t MetaState::Update(int32_t android_keycode, bool down) {
  int32_t flags = ModifierFlags(android_keycode);
  if (flags == 0) {
    return state_;
  }
  // Only the side-specific bit is tracked per key; the generic *_ON bit stays
  // set while either side is held.
  int32_t side =
      flags & ~(kMetaShiftOn | kMetaAltOn | kMetaCtrlOn | kMetaMetaOn);
  int32_t generic = flags & ~side;
  if (down) {
    state_ |= flags;
  } else {
    state_ &= ~side;
    static constexpr int32_t kSides[][2] = {
        {kMetaShiftOn, kMetaShiftLeftOn | kMetaShiftRightOn},
        {kMetaAltOn, kMetaAltLeftOn | kMetaAltRightOn},
        {kMetaCtrlOn, kMetaCtrlLeftOn | kMetaCtrlRightOn},
        {kMetaMetaOn, kMetaMetaLeftOn | kMetaMetaRightOn},
    };
    for (const auto& [on, sides] : kSides) {
      if (generic == on && !(state_ & sides)) {
        state_ &= ~on;
      }
    }
  }
  return state_;
}

}  // namespace redroid
