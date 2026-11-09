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
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "json/json.h"

#include "cuttlefish/host/frontend/redroid_webrtc/keymap.h"
#include "cuttlefish/host/frontend/webrtc/adb_handler.h"
#include "cuttlefish/host/frontend/webrtc/libdevice/connection_observer.h"
#include "cuttlefish/result/result.h"

namespace redroid {

using cuttlefish::Result;

// Sends input to the device. Shared by all client connections; the underlying
// scrcpy session can be replaced when it reconnects.
class DeviceInput {
 public:
  using Sender = std::function<bool(const std::vector<uint8_t>&)>;

  void SetSender(Sender sender);
  void SetScreenSize(int width, int height);
  bool Send(const std::vector<uint8_t>& message);

  int width() const { return width_; }
  int height() const { return height_; }

  // Called whenever a frame is needed right away (e.g. a client connected).
  std::function<void()> on_client_connected;
  // Rotates the device display by quarter turns (1 = 90deg counterclockwise,
  // like the web UI's rotate left button).
  std::function<void(int quarter_turns)> on_rotate;

 private:
  std::mutex mutex_;
  Sender sender_;
  std::atomic<int> width_{0};
  std::atomic<int> height_{0};
};

// Translates browser input (decoded by the cuttlefish streamer library) into
// scrcpy control messages, and bridges the adb data channel to adbd.
class RedroidConnectionObserver
    : public cuttlefish::webrtc_streaming::ConnectionObserver {
 public:
  RedroidConnectionObserver(std::shared_ptr<DeviceInput> input,
                            std::string adb_address);
  ~RedroidConnectionObserver() override = default;

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
  Result<void> Touch(int64_t pointer_id, int x, int y, bool down);
  Result<void> Key(int32_t android_keycode, bool down);

  std::shared_ptr<DeviceInput> input_;
  std::string adb_address_;
  std::unique_ptr<cuttlefish::webrtc_streaming::AdbHandler> adb_handler_;
  std::function<bool(const Json::Value)> control_sender_;
  std::mutex mutex_;
  std::set<int64_t> active_pointers_;
  std::set<int32_t> pressed_keys_;
  MetaState meta_state_;
  int last_x_ = -1;
  int last_y_ = -1;
  bool mouse_down_ = false;
};

class RedroidConnectionObserverFactory
    : public cuttlefish::webrtc_streaming::ConnectionObserverFactory {
 public:
  RedroidConnectionObserverFactory(std::shared_ptr<DeviceInput> input,
                                   std::string adb_address)
      : input_(std::move(input)), adb_address_(std::move(adb_address)) {}

  std::shared_ptr<cuttlefish::webrtc_streaming::ConnectionObserver>
  CreateObserver() override {
    return std::make_shared<RedroidConnectionObserver>(input_, adb_address_);
  }

 private:
  std::shared_ptr<DeviceInput> input_;
  std::string adb_address_;
};

}  // namespace redroid
