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

#include "cuttlefish/host/frontend/redroid_webrtc/redroid_connection_observer.h"

#include <stdio.h>

#include <algorithm>

#include "absl/log/log.h"

#include "cuttlefish/host/frontend/redroid_webrtc/scrcpy_protocol.h"

namespace redroid {

void DeviceInput::SetSender(Sender sender) {
  std::lock_guard<std::mutex> lock(mutex_);
  sender_ = std::move(sender);
}

void DeviceInput::SetScreenSize(int width, int height) {
  width_ = width;
  height_ = height;
}

bool DeviceInput::Send(const std::vector<uint8_t>& message) {
  std::lock_guard<std::mutex> lock(mutex_);
  return sender_ && sender_(message);
}

RedroidConnectionObserver::RedroidConnectionObserver(
    std::shared_ptr<DeviceInput> input, std::string adb_address)
    : input_(std::move(input)), adb_address_(std::move(adb_address)) {}

void RedroidConnectionObserver::OnConnected() {
  LOG(INFO) << "Client connected";
  if (input_->on_client_connected) {
    input_->on_client_connected();
  }
}

Result<void> RedroidConnectionObserver::Touch(int64_t pointer_id, int x, int y,
                                              bool down) {
  std::lock_guard<std::mutex> lock(mutex_);
  uint8_t action;
  bool active = active_pointers_.count(pointer_id) > 0;
  if (down) {
    action = active ? scrcpy::kActionMove : scrcpy::kActionDown;
    active_pointers_.insert(pointer_id);
  } else {
    if (!active) {
      return {};  // UP without DOWN, nothing to release.
    }
    action = scrcpy::kActionUp;
    active_pointers_.erase(pointer_id);
  }
  last_x_ = x;
  last_y_ = y;
  // The server drops events whose screen size differs from the current video
  // size, so always use the size of the frames the client is looking at.
  input_->Send(scrcpy::InjectTouch(
      action, pointer_id, x, y, static_cast<uint16_t>(input_->width()),
      static_cast<uint16_t>(input_->height()), down ? 1.0f : 0.0f,
      /*action_button=*/0, /*buttons=*/0));
  return {};
}

Result<void> RedroidConnectionObserver::Key(int32_t android_keycode,
                                            bool down) {
  int32_t meta;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    // The web UI's buttons also send "up" when the pointer merely leaves
    // them; Android handles an unpaired up as a press, so drop those.
    if (down) {
      pressed_keys_.insert(android_keycode);
    } else if (!pressed_keys_.erase(android_keycode)) {
      return {};
    }
    meta = meta_state_.Update(android_keycode, down);
  }
  input_->Send(scrcpy::InjectKeycode(
      down ? scrcpy::kActionDown : scrcpy::kActionUp, android_keycode,
      /*repeat=*/0, meta));
  return {};
}

Result<void> RedroidConnectionObserver::OnMouseMoveEvent(int x, int y) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    last_x_ = x;
    last_y_ = y;
    if (!mouse_down_) {
      return {};
    }
  }
  return Touch(scrcpy::kPointerIdGenericFinger, x, y, true);
}

Result<void> RedroidConnectionObserver::OnMouseButtonEvent(int button,
                                                           bool down) {
  if (button != 0) {
    // Secondary button behaves like Android's back, as in scrcpy.
    if (button == 2) {
      return Key(kAndroidKeycodeBack, down);
    }
    return {};
  }
  int x, y;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    mouse_down_ = down;
    x = last_x_;
    y = last_y_;
  }
  return Touch(scrcpy::kPointerIdGenericFinger, x, y, down);
}

Result<void> RedroidConnectionObserver::OnMouseWheelEvent(int pixels) {
  return OnRotaryWheelEvent(pixels);
}

Result<void> RedroidConnectionObserver::OnGamepadKeyEvent(int, bool) {
  return {};
}

Result<void> RedroidConnectionObserver::OnGamepadMotionEvent(int, int) {
  return {};
}

Result<void> RedroidConnectionObserver::OnTouchEvent(const std::string&, int x,
                                                     int y, bool down) {
  return Touch(scrcpy::kPointerIdGenericFinger, x, y, down);
}

Result<void> RedroidConnectionObserver::OnMultiTouchEvent(const std::string&,
                                                          Json::Value id,
                                                          Json::Value x,
                                                          Json::Value y,
                                                          bool down, int size) {
  for (int i = 0; i < size; i++) {
    // Browser touch ids are small non negative integers; mouse-generated
    // touches use id 0 as well, which is fine as a finger.
    CF_EXPECT(Touch(id[i].asInt64(), x[i].asInt(), y[i].asInt(), down));
  }
  return {};
}

Result<void> RedroidConnectionObserver::OnKeyboardEvent(uint16_t keycode,
                                                        bool down) {
  auto android_keycode = LinuxToAndroidKeycode(keycode);
  if (!android_keycode) {
    VLOG(1) << "Ignoring unmapped linux keycode " << keycode;
    return {};
  }
  return Key(*android_keycode, down);
}

Result<void> RedroidConnectionObserver::OnRotaryWheelEvent(int pixels) {
  int x, y;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    x = last_x_ >= 0 ? last_x_ : input_->width() / 2;
    y = last_y_ >= 0 ? last_y_ : input_->height() / 2;
  }
  // Browsers report ~100 pixels per wheel notch; positive = scroll down,
  // which is a negative vertical scroll for Android.
  float vscroll = std::clamp(-pixels / 100.0f, -16.0f, 16.0f);
  input_->Send(scrcpy::InjectScroll(
      x, y, static_cast<uint16_t>(input_->width()),
      static_cast<uint16_t>(input_->height()), 0.0f, vscroll, 0));
  return {};
}

void RedroidConnectionObserver::OnAdbChannelOpen(
    std::function<bool(const uint8_t*, size_t)> adb_message_sender) {
  LOG(INFO) << "adb channel open, bridging to " << adb_address_;
  adb_handler_ = std::make_unique<cuttlefish::webrtc_streaming::AdbHandler>(
      adb_address_, [adb_message_sender](const uint8_t* msg, size_t size) {
        adb_message_sender(msg, size);
      });
}

void RedroidConnectionObserver::OnAdbMessage(const uint8_t* msg, size_t size) {
  if (adb_handler_) {
    adb_handler_->handleMessage(msg, size);
  }
}

void RedroidConnectionObserver::OnControlChannelOpen(
    std::function<bool(const Json::Value)> control_message_sender) {
  std::lock_guard<std::mutex> lock(mutex_);
  control_sender_ = std::move(control_message_sender);
}

Result<void> RedroidConnectionObserver::OnLidStateChange(bool) { return {}; }
void RedroidConnectionObserver::OnHingeAngleChange(int) {}

Result<void> RedroidConnectionObserver::OnPowerButton(bool button_down) {
  return Key(kAndroidKeycodePower, button_down);
}

Result<void> RedroidConnectionObserver::OnBackButton(bool button_down) {
  return Key(kAndroidKeycodeBack, button_down);
}

Result<void> RedroidConnectionObserver::OnHomeButton(bool button_down) {
  return Key(kAndroidKeycodeHome, button_down);
}

Result<void> RedroidConnectionObserver::OnMenuButton(bool button_down) {
  return Key(kAndroidKeycodeAppSwitch, button_down);
}

Result<void> RedroidConnectionObserver::OnVolumeDownButton(bool button_down) {
  return Key(kAndroidKeycodeVolumeDown, button_down);
}

Result<void> RedroidConnectionObserver::OnVolumeUpButton(bool button_down) {
  return Key(kAndroidKeycodeVolumeUp, button_down);
}

void RedroidConnectionObserver::OnCustomActionButton(const std::string&,
                                                     const std::string&) {}
void RedroidConnectionObserver::OnCameraControlMsg(const Json::Value&) {}
void RedroidConnectionObserver::OnDisplayControlMsg(const Json::Value&) {}
void RedroidConnectionObserver::OnDisplayAddMsg(const Json::Value&) {}
void RedroidConnectionObserver::OnDisplayRemoveMsg(const Json::Value&) {}
void RedroidConnectionObserver::OnBluetoothChannelOpen(
    std::function<bool(const uint8_t*, size_t)>) {}
void RedroidConnectionObserver::OnBluetoothMessage(const uint8_t*, size_t) {}
void RedroidConnectionObserver::OnSensorsChannelOpen(
    std::function<bool(const uint8_t*, size_t)>) {}
// The rotate buttons send the device's new orientation as sensor angles
// "x y z" (z = the client's rotation) and rotate the video with CSS. redroid
// has no sensors, so rotate the display instead, then reset the client's CSS
// rotation: the stream itself turns. Each press therefore arrives as +-90.
void RedroidConnectionObserver::OnSensorsMessage(const uint8_t* msg,
                                                 size_t size) {
  double x = 0, y = 0, z = 0;
  std::string text(reinterpret_cast<const char*>(msg), size);
  if (sscanf(text.c_str(), "%lf %lf %lf", &x, &y, &z) != 3 || x != 0 ||
      y != 0) {
    return;
  }
  int quarter_turns = (static_cast<int>(z) / 90 % 4 + 4) % 4;
  if (static_cast<int>(z) % 90 != 0 || quarter_turns == 0) {
    return;
  }
  if (input_->on_rotate) {
    input_->on_rotate(quarter_turns);
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (control_sender_) {
    Json::Value event;
    event["event"] = "VIRTUAL_DEVICE_SCREEN_CHANGED";
    event["metadata"]["rotation"] = 0;
    control_sender_(event);
  }
}
void RedroidConnectionObserver::OnSensorsChannelClosed() {}
void RedroidConnectionObserver::OnLightsChannelOpen(
    std::function<bool(const Json::Value&)>) {}
void RedroidConnectionObserver::OnLightsChannelClosed() {}
void RedroidConnectionObserver::OnLocationChannelOpen(
    std::function<bool(const uint8_t*, size_t)>) {}
void RedroidConnectionObserver::OnLocationMessage(const uint8_t*, size_t) {}
void RedroidConnectionObserver::OnKmlLocationsChannelOpen(
    std::function<bool(const uint8_t*, size_t)>) {}
void RedroidConnectionObserver::OnGpxLocationsChannelOpen(
    std::function<bool(const uint8_t*, size_t)>) {}
void RedroidConnectionObserver::OnKmlLocationsMessage(const uint8_t*, size_t) {}
void RedroidConnectionObserver::OnGpxLocationsMessage(const uint8_t*, size_t) {}
void RedroidConnectionObserver::OnCameraData(const std::vector<char>&) {}

}  // namespace redroid
