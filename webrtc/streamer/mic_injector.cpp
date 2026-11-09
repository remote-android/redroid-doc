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

#include "cuttlefish/host/frontend/redroid_webrtc/mic_injector.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <vector>

#include "absl/log/log.h"

namespace redroid {
namespace {

constexpr int kSampleRate = 48000;
constexpr int kSamplesPer10Ms = kSampleRate / 100;
constexpr auto kHold = std::chrono::seconds(5);
constexpr auto kRetryDelay = std::chrono::seconds(5);
constexpr auto kPeriod = std::chrono::milliseconds(10);
// WebRTC's output is not digital silence without a microphone: it dithers
// around +-1.
constexpr int kSilencePeak = 16;
constexpr char kRemotePath[] = "/data/local/tmp/redroid-mic-injector.dex";

}  // namespace

bool HasSignal(const int16_t* samples, size_t count) {
  return std::any_of(samples, samples + count, [](int16_t s) {
    return s > kSilencePeak || s < -kSilencePeak;
  });
}

MicInjector::MicInjector(
    const Adb& adb, std::string helper_path,
    std::shared_ptr<cuttlefish::webrtc_streaming::AudioSource> source)
    : adb_(adb),
      helper_path_(std::move(helper_path)),
      source_(std::move(source)) {}

MicInjector::~MicInjector() { Stop(); }

void MicInjector::Start() {
  running_ = true;
  thread_ = std::thread([this] { Loop(); });
}

void MicInjector::Stop() {
  running_ = false;
  if (thread_.joinable()) {
    thread_.join();
  }
}

bool MicInjector::StartHelper() {
  // Pushed every time: the device may have been reset since the last start.
  if (!adb_.Run({"push", helper_path_, kRemotePath}, /*quiet=*/true)) {
    LOG(WARNING) << "Failed to push the microphone helper";
    return false;
  }
  helper_pid_ = adb_.SpawnWithStdin(
      {"shell", std::string("CLASSPATH=") + kRemotePath +
                    " exec app_process / redroid.MicInjector"},
      &helper_stdin_);
  if (helper_pid_ < 0) {
    return false;
  }
  // If the device falls behind, drop audio instead of adding latency.
  fcntl(helper_stdin_, F_SETPIPE_SZ, 4096);
  fcntl(helper_stdin_, F_SETFL, fcntl(helper_stdin_, F_GETFL) | O_NONBLOCK);
  LOG(INFO) << "Microphone on";
  return true;
}

void MicInjector::StopHelper() {
  if (helper_stdin_ >= 0) {
    close(helper_stdin_);
    helper_stdin_ = -1;
  }
  if (helper_pid_ >= 0) {
    // Closing stdin ends the device process; adb then exits by itself.
    for (int i = 0; i < 20; i++) {
      if (waitpid(helper_pid_, nullptr, WNOHANG) != 0) {
        helper_pid_ = -1;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (helper_pid_ >= 0) {
      kill(helper_pid_, SIGTERM);
      waitpid(helper_pid_, nullptr, 0);
      helper_pid_ = -1;
    }
  }
  LOG(INFO) << "Microphone off";
}

void MicInjector::Loop() {
  std::vector<int16_t> samples(kSamplesPer10Ms);
  MicGate gate(kHold);
  auto retry_at = MicGate::Clock::time_point::min();
  auto next = MicGate::Clock::now();
  while (running_) {
    next += kPeriod;
    std::this_thread::sleep_until(next);
    bool muted = false;
    int read =
        source_->GetMoreAudioData(samples.data(), sizeof(int16_t),
                                  kSamplesPer10Ms, 1, kSampleRate, muted);
    bool valid = !muted && read > 0;
    if (!valid) {
      std::fill(samples.begin(), samples.end(), 0);
    }
    auto now = MicGate::Clock::now();
    bool wanted = gate.Update(valid && HasSignal(samples.data(), read), now);

    if (!wanted) {
      if (helper_pid_ >= 0) {
        StopHelper();
      }
      continue;
    }
    if (helper_pid_ < 0) {
      if (now < retry_at) {
        continue;
      }
      if (!StartHelper()) {
        retry_at = now + kRetryDelay;
        continue;
      }
    }
    ssize_t written =
        write(helper_stdin_, samples.data(), samples.size() * sizeof(int16_t));
    if (written < 0 && errno != EAGAIN) {
      LOG(WARNING) << "Microphone helper exited";
      StopHelper();
      retry_at = now + kRetryDelay;
    }
  }
  if (helper_pid_ >= 0) {
    StopHelper();
  }
}

}  // namespace redroid
