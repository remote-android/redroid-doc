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

#include "cuttlefish/host/frontend/redroid_webrtc/logging_connection_observer.h"

#include "absl/log/log.h"

namespace redroid {

LoggingConnectionObserver::LoggingConnectionObserver(
    std::shared_ptr<PointerState> pointer)
    : pointer_(std::move(pointer)) {}

void LoggingConnectionObserver::OnConnected() {
  LOG(INFO) << "Client connected";
}

Result<void> LoggingConnectionObserver::OnMouseMoveEvent(int x, int y) {
  pointer_->x = x;
  pointer_->y = y;
  return {};
}

Result<void> LoggingConnectionObserver::OnMouseButtonEvent(int button,
                                                           bool down) {
  LOG(INFO) << "mouse button=" << button << " down=" << down;
  pointer_->down = down;
  return {};
}

Result<void> LoggingConnectionObserver::OnMouseWheelEvent(int pixels) {
  LOG(INFO) << "mouse wheel pixels=" << pixels;
  return {};
}

Result<void> LoggingConnectionObserver::OnGamepadKeyEvent(int button,
                                                          bool down) {
  LOG(INFO) << "gamepad key=" << button << " down=" << down;
  return {};
}

Result<void> LoggingConnectionObserver::OnGamepadMotionEvent(int code,
                                                             int value) {
  LOG(INFO) << "gamepad motion code=" << code << " value=" << value;
  return {};
}

Result<void> LoggingConnectionObserver::OnTouchEvent(
    const std::string& device_label, int x, int y, bool down) {
  LOG(INFO) << "touch " << device_label << " x=" << x << " y=" << y
            << " down=" << down;
  pointer_->x = x;
  pointer_->y = y;
  pointer_->down = down;
  return {};
}

Result<void> LoggingConnectionObserver::OnMultiTouchEvent(
    const std::string& label, Json::Value id, Json::Value x, Json::Value y,
    bool down, int size) {
  LOG(INFO) << "multi-touch " << label << " contacts=" << size
            << " down=" << down;
  if (size > 0) {
    pointer_->x = x[0].asInt();
    pointer_->y = y[0].asInt();
    pointer_->down = down;
  }
  return {};
}

Result<void> LoggingConnectionObserver::OnKeyboardEvent(uint16_t keycode,
                                                        bool down) {
  LOG(INFO) << "keyboard linux_keycode=" << keycode << " down=" << down;
  return {};
}

Result<void> LoggingConnectionObserver::OnRotaryWheelEvent(int pixels) {
  LOG(INFO) << "rotary pixels=" << pixels;
  return {};
}

void LoggingConnectionObserver::OnAdbChannelOpen(
    std::function<bool(const uint8_t*, size_t)>) {
  LOG(INFO) << "adb channel opened (not supported in spike)";
}

void LoggingConnectionObserver::OnAdbMessage(const uint8_t*, size_t size) {
  LOG(INFO) << "adb message bytes=" << size;
}

void LoggingConnectionObserver::OnControlChannelOpen(
    std::function<bool(const Json::Value)>) {
  LOG(INFO) << "control channel opened";
}

Result<void> LoggingConnectionObserver::OnLidStateChange(bool lid_open) {
  LOG(INFO) << "lid open=" << lid_open;
  return {};
}

void LoggingConnectionObserver::OnHingeAngleChange(int hinge_angle) {
  LOG(INFO) << "hinge angle=" << hinge_angle;
}

Result<void> LoggingConnectionObserver::OnPowerButton(bool button_down) {
  LOG(INFO) << "power down=" << button_down;
  return {};
}

Result<void> LoggingConnectionObserver::OnBackButton(bool button_down) {
  LOG(INFO) << "back down=" << button_down;
  return {};
}

Result<void> LoggingConnectionObserver::OnHomeButton(bool button_down) {
  LOG(INFO) << "home down=" << button_down;
  return {};
}

Result<void> LoggingConnectionObserver::OnMenuButton(bool button_down) {
  LOG(INFO) << "menu down=" << button_down;
  return {};
}

Result<void> LoggingConnectionObserver::OnVolumeDownButton(bool button_down) {
  LOG(INFO) << "volume- down=" << button_down;
  return {};
}

Result<void> LoggingConnectionObserver::OnVolumeUpButton(bool button_down) {
  LOG(INFO) << "volume+ down=" << button_down;
  return {};
}

void LoggingConnectionObserver::OnCustomActionButton(
    const std::string& command, const std::string& button_state) {
  LOG(INFO) << "custom action " << command << " state=" << button_state;
}

void LoggingConnectionObserver::OnCameraControlMsg(const Json::Value&) {}
void LoggingConnectionObserver::OnDisplayControlMsg(const Json::Value&) {}
void LoggingConnectionObserver::OnDisplayAddMsg(const Json::Value&) {}
void LoggingConnectionObserver::OnDisplayRemoveMsg(const Json::Value&) {}

void LoggingConnectionObserver::OnBluetoothChannelOpen(
    std::function<bool(const uint8_t*, size_t)>) {}
void LoggingConnectionObserver::OnBluetoothMessage(const uint8_t*, size_t) {}
void LoggingConnectionObserver::OnSensorsChannelOpen(
    std::function<bool(const uint8_t*, size_t)>) {}
void LoggingConnectionObserver::OnSensorsMessage(const uint8_t*, size_t) {}
void LoggingConnectionObserver::OnSensorsChannelClosed() {}
void LoggingConnectionObserver::OnLightsChannelOpen(
    std::function<bool(const Json::Value&)>) {}
void LoggingConnectionObserver::OnLightsChannelClosed() {}
void LoggingConnectionObserver::OnLocationChannelOpen(
    std::function<bool(const uint8_t*, size_t)>) {}
void LoggingConnectionObserver::OnLocationMessage(const uint8_t*, size_t) {}
void LoggingConnectionObserver::OnKmlLocationsChannelOpen(
    std::function<bool(const uint8_t*, size_t)>) {}
void LoggingConnectionObserver::OnGpxLocationsChannelOpen(
    std::function<bool(const uint8_t*, size_t)>) {}
void LoggingConnectionObserver::OnKmlLocationsMessage(const uint8_t*, size_t) {}
void LoggingConnectionObserver::OnGpxLocationsMessage(const uint8_t*, size_t) {}

void LoggingConnectionObserver::OnCameraData(const std::vector<char>&) {}

}  // namespace redroid
