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
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "cuttlefish/host/frontend/redroid_webrtc/adb.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/audio_source.h"

namespace redroid {

// Decides when the device helper runs. WebRTC reports its audio source as
// playing as soon as any client connects, so only samples tell whether a
// client's microphone is on. Silence shorter than `hold` keeps it open.
class MicGate {
 public:
  using Clock = std::chrono::steady_clock;

  explicit MicGate(Clock::duration hold) : hold_(hold) {}

  // Returns whether the helper should be running now.
  bool Update(bool has_signal, Clock::time_point now) {
    if (has_signal) {
      last_signal_ = now;
    }
    return last_signal_ && now - *last_signal_ < hold_;
  }

 private:
  Clock::duration hold_;
  std::optional<Clock::time_point> last_signal_;
};

// True if any 16 bit sample is louder than the noise of an idle stream.
bool HasSignal(const int16_t* samples, size_t count);

// Sends the audio of the clients' microphones to the device's microphone
// through mic-helper/ (see MicInjector.java). The helper only runs while a
// client sends audio, so nothing changes on the device otherwise.
class MicInjector {
 public:
  MicInjector(
      const Adb& adb, std::string helper_path,
      std::shared_ptr<cuttlefish::webrtc_streaming::AudioSource> source);
  ~MicInjector();

  void Start();
  void Stop();

 private:
  void Loop();
  bool StartHelper();
  void StopHelper();

  const Adb& adb_;
  std::string helper_path_;
  std::shared_ptr<cuttlefish::webrtc_streaming::AudioSource> source_;
  std::atomic<bool> running_{false};
  std::thread thread_;
  pid_t helper_pid_ = -1;
  int helper_stdin_ = -1;
};

}  // namespace redroid
