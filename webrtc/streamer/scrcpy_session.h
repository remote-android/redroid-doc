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

#include <sys/types.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "cuttlefish/host/frontend/redroid_webrtc/adb.h"
#include "cuttlefish/host/frontend/redroid_webrtc/scrcpy_protocol.h"

namespace redroid {

// Runs a scrcpy server on the device (through adb) and exposes its video,
// audio and control streams.
class ScrcpySession {
 public:
  struct Options {
    std::string server_path;     // local path of scrcpy-server.jar
    std::string server_version;  // must match the jar, e.g. "3.3.1"
    int local_port = 27183;      // local end of `adb forward`
    int max_fps = 30;
    int video_bit_rate = 8000000;
    bool video = true;  // false: audio and control only
    bool audio = true;
  };

  struct Callbacks {
    // Called from the video thread for every packet (config packets included).
    std::function<void(const scrcpy::PacketHeader&, const uint8_t*, size_t)>
        on_video_packet;
    // Called from the audio thread with raw s16le stereo 48kHz PCM.
    std::function<void(const uint8_t*, size_t)> on_audio_packet;
    // Called once when any stream ends (device gone, server crashed...).
    std::function<void()> on_disconnected;
  };

  ScrcpySession(const Adb& adb, Options options, Callbacks callbacks);
  ~ScrcpySession();

  // Starts the server and connects the streams. Blocks until the stream
  // headers are received. On success, packets are delivered from background
  // threads until Stop() is called or the connection drops.
  bool Start();
  void Stop();

  bool SendControl(const std::vector<uint8_t>& message);

  const std::string& device_name() const { return device_name_; }
  const scrcpy::VideoHeader& video_header() const { return video_header_; }
  bool audio_enabled() const { return audio_fd_ >= 0; }

 private:
  bool ConnectSockets();
  void VideoLoop();
  void AudioLoop();
  void ControlReadLoop();
  void NotifyDisconnected();
  void CloseSockets();

  const Adb& adb_;
  Options options_;
  Callbacks callbacks_;
  uint32_t scid_ = 0;
  pid_t server_pid_ = -1;
  int video_fd_ = -1;
  int audio_fd_ = -1;
  int control_fd_ = -1;
  std::string device_name_;
  scrcpy::VideoHeader video_header_{};
  std::mutex control_mutex_;
  std::atomic<bool> stopping_{false};
  std::atomic<bool> disconnected_notified_{false};
  std::vector<std::thread> threads_;
};

}  // namespace redroid
