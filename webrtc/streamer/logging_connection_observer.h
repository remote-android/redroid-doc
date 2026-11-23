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

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "json/json.h"

#include "cuttlefish/host/frontend/webrtc/libdevice/connection_observer.h"
#include "cuttlefish/result/result.h"

namespace redroid {

using cuttlefish::Result;

// Last pointer position reported by any client, shared with the frame source
// so that input can be visualised in the test pattern.
struct PointerState {
  std::atomic<int> x{-1};
  std::atomic<int> y{-1};
  std::atomic<bool> down{false};
};

// A ConnectionObserver that only logs what the browser sends and records the
// pointer position. Used by the Phase 0 spike; real devices replace it with an
// observer that forwards input to redroid.
class LoggingConnectionObserver
    : public cuttlefish::webrtc_streaming::ConnectionObserver {
 public:
  explicit LoggingConnectionObserver(std::shared_ptr<PointerState> pointer);
  ~LoggingConnectionObserver() override = default;

  void OnConnected() override;

  Result<void> OnMouseMoveEvent(int x, int y) override;
  Result<void> OnMouseButtonEvent(int button, bool down) override;
  Result<void> OnMouseWheelEvent(int pixels) override;
  Result<void> OnGamepadKeyEvent(int button, bool down) override;
  Result<void> OnGamepadMotionEvent(int code, int value) override;
  Result<void> OnTouchEvent(const std::string& device_label, int x, int y,
                            bool down) override;
  Result<void> OnMultiTouchEvent(const std::string& label, Json::Value id,
                                 Json::Value x, Json::Value y, bool down,
                                 int size) override;
  Result<void> OnKeyboardEvent(uint16_t keycode, bool down) override;
  Result<void> OnRotaryWheelEvent(int pixels) override;

  void OnAdbChannelOpen(
      std::function<bool(const uint8_t*, size_t)> adb_message_sender) override;
  void OnAdbMessage(const uint8_t* msg, size_t size) override;

  void OnControlChannelOpen(
      std::function<bool(const Json::Value)> control_message_sender) override;
  Result<void> OnLidStateChange(bool lid_open) override;
  void OnHingeAngleChange(int hinge_angle) override;
  Result<void> OnPowerButton(bool button_down) override;
  Result<void> OnBackButton(bool button_down) override;
  Result<void> OnHomeButton(bool button_down) override;
  Result<void> OnMenuButton(bool button_down) override;
  Result<void> OnVolumeDownButton(bool button_down) override;
  Result<void> OnVolumeUpButton(bool button_down) override;
  void OnCustomActionButton(const std::string& command,
                            const std::string& button_state) override;

  void OnCameraControlMsg(const Json::Value& msg) override;
  void OnDisplayControlMsg(const Json::Value& msg) override;
  void OnDisplayAddMsg(const Json::Value& msg) override;
  void OnDisplayRemoveMsg(const Json::Value& msg) override;

  void OnBluetoothChannelOpen(std::function<bool(const uint8_t*, size_t)>
                                  bluetooth_message_sender) override;
  void OnBluetoothMessage(const uint8_t* msg, size_t size) override;
  void OnSensorsChannelOpen(std::function<bool(const uint8_t*, size_t)>
                                sensors_message_sender) override;
  void OnSensorsMessage(const uint8_t* msg, size_t size) override;
  void OnSensorsChannelClosed() override;
  void OnLightsChannelOpen(
      std::function<bool(const Json::Value&)> lights_message_sender) override;
  void OnLightsChannelClosed() override;
  void OnLocationChannelOpen(std::function<bool(const uint8_t*, size_t)>
                                 location_message_sender) override;
  void OnLocationMessage(const uint8_t* msg, size_t size) override;
  void OnKmlLocationsChannelOpen(std::function<bool(const uint8_t*, size_t)>
                                     kml_locations_message_sender) override;
  void OnGpxLocationsChannelOpen(std::function<bool(const uint8_t*, size_t)>
                                     gpx_locations_message_sender) override;
  void OnKmlLocationsMessage(const uint8_t* msg, size_t size) override;
  void OnGpxLocationsMessage(const uint8_t* msg, size_t size) override;

  void OnCameraData(const std::vector<char>& data) override;

 private:
  std::shared_ptr<PointerState> pointer_;
};

class LoggingConnectionObserverFactory
    : public cuttlefish::webrtc_streaming::ConnectionObserverFactory {
 public:
  explicit LoggingConnectionObserverFactory(
      std::shared_ptr<PointerState> pointer)
      : pointer_(std::move(pointer)) {}

  std::shared_ptr<cuttlefish::webrtc_streaming::ConnectionObserver>
  CreateObserver() override {
    return std::make_shared<LoggingConnectionObserver>(pointer_);
  }

 private:
  std::shared_ptr<PointerState> pointer_;
};

}  // namespace redroid
