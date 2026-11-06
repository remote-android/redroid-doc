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

// redroid_webrtc: streams a redroid device to browsers through the cuttlefish
// WebRTC operator.
//
// --source=scrcpy (default): video, audio and input go through a scrcpy
//   server started on the device over adb.
// --source=test_pattern: renders a test pattern and logs input, to check the
//   WebRTC setup without a device.

#include <gflags/gflags.h>
#include <signal.h>
#include <stdint.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <vector>

#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "drm/drm_fourcc.h"

#include "cuttlefish/host/frontend/redroid_webrtc/adb.h"
#include "cuttlefish/host/frontend/redroid_webrtc/h264_decoder.h"
#include "cuttlefish/host/frontend/redroid_webrtc/h264_passthrough.h"
#include "cuttlefish/host/frontend/redroid_webrtc/hwc_frame_source.h"
#include "cuttlefish/host/frontend/redroid_webrtc/logging_connection_observer.h"
#include "cuttlefish/host/frontend/redroid_webrtc/mic_injector.h"
#include "cuttlefish/host/frontend/redroid_webrtc/redroid_connection_observer.h"
#include "cuttlefish/host/frontend/redroid_webrtc/scrcpy_protocol.h"
#include "cuttlefish/host/frontend/redroid_webrtc/scrcpy_session.h"
#include "cuttlefish/host/frontend/webrtc/client_server.h"
#include "cuttlefish/host/frontend/webrtc/cvd_abgr_video_frame_buffer.h"
#include "cuttlefish/host/frontend/webrtc/libdevice/audio_frame_buffer.h"
#include "cuttlefish/host/frontend/webrtc/libdevice/audio_sink.h"
#include "cuttlefish/host/frontend/webrtc/libdevice/recording_manager.h"
#include "cuttlefish/host/frontend/webrtc/libdevice/streamer.h"
#include "cuttlefish/host/frontend/webrtc/libdevice/video_sink.h"

DEFINE_string(source, "scrcpy",
              "Frame source: scrcpy (device H.264 encoder), hwc (raw frames "
              "from redroid's hwcomposer stream socket) or test_pattern.");
DEFINE_string(hwc_socket, "",
              "--source=hwc: path of the device's /ipc/hwcomposer.sock "
              "(redroid started with androidboot.use_redroid_stream=1).");
DEFINE_string(device_id, "redroid",
              "Id the device registers with at the operator.");
DEFINE_string(operator_path, "/run/cuttlefish/operator",
              "Unix socket of the cuttlefish operator.");
DEFINE_string(client_dir, "/usr/share/redroid-webrtc/html_client",
              "Directory with the cuttlefish html client files.");
DEFINE_string(adb, "adb", "Path of the adb client.");
DEFINE_string(adb_serial, "127.0.0.1:5555",
              "adb serial of the redroid container (host:port).");
DEFINE_string(scrcpy_server, "/usr/share/redroid-webrtc/scrcpy-server",
              "Path of the scrcpy server jar.");
DEFINE_string(scrcpy_version, "3.3.1", "Version of the scrcpy server jar.");
DEFINE_int32(scrcpy_port, 27183,
             "Local port used to forward the scrcpy sockets.");
DEFINE_int32(video_bit_rate, 8000000, "Bit rate of the device H.264 encoder.");
DEFINE_bool(audio, true, "Stream device audio (Android 11+).");
DEFINE_string(mic_helper, "/usr/share/redroid-webrtc/mic-injector.dex",
              "Dex of the device microphone helper, empty to disable the "
              "microphone.");
DEFINE_bool(raise_capture_volume, true,
            "Set the media volume of the captured output to the maximum the "
            "first time a device is streamed (Android's default of 5/15 "
            "makes the stream ~33 dB quieter). Later changes are kept.");
DEFINE_bool(
    video_passthrough, false,
    "Send the device's H.264 stream to browsers as is instead of decoding "
    "and re-encoding it (VP8). Much cheaper; needs H.264 in the browser.");
DEFINE_int32(width, 720, "Display width (test_pattern only).");
DEFINE_int32(height, 1280, "Display height (test_pattern only).");
DEFINE_int32(dpi, 0, "Display dpi, 0 to read it from the device.");
DEFINE_int32(fps, 30, "Max frames per second.");
DEFINE_int32(udp_port_min, 15550, "First UDP port WebRTC may use.");
DEFINE_int32(udp_port_max, 15599, "Last UDP port WebRTC may use.");
DEFINE_int32(boot_timeout_s, 600, "How long to wait for the device to boot.");

namespace redroid {
namespace {

using cuttlefish::CvdAbgrVideoFrameBuffer;
using cuttlefish::VideoFrameBuffer;
using cuttlefish::webrtc_streaming::AudioFrameBuffer;
using cuttlefish::webrtc_streaming::AudioSink;
using cuttlefish::webrtc_streaming::ConnectionObserverFactory;
using cuttlefish::webrtc_streaming::OperatorObserver;
using cuttlefish::webrtc_streaming::RecordingManager;
using cuttlefish::webrtc_streaming::Streamer;
using cuttlefish::webrtc_streaming::StreamerConfig;
using cuttlefish::webrtc_streaming::VideoSink;

constexpr auto kDisplayLabel = "display_0";
constexpr auto kAudioLabel = "audio-0";

std::atomic<bool> g_running{true};
// Rotation polls left at a short interval, after a rotation request.
std::atomic<int> g_fast_rotation_polls{0};

int64_t NowUs() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

class LoggingOperatorObserver : public OperatorObserver {
 public:
  void OnRegistered() override { LOG(INFO) << "Registered with operator"; }
  void OnClose() override { LOG(ERROR) << "Connection with operator closed"; }
  void OnError() override { LOG(ERROR) << "Error in connection with operator"; }
};

// Holds the latest frame and re-sends it while the screen is idle, so new
// clients and the encoder always have a recent picture (scrcpy only sends
// frames when the screen content changes).
class FrameRepeater {
 public:
  // repeat=false only forwards frames: encoded frames must not be repeated.
  FrameRepeater(std::shared_ptr<VideoSink> sink, bool repeat)
      : sink_(std::move(sink)), repeat_(repeat), thread_([this] { Loop(); }) {}

  ~FrameRepeater() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_ = true;
    }
    cv_.notify_all();
    thread_.join();
  }

  void OnFrame(std::shared_ptr<VideoFrameBuffer> frame) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      last_frame_ = frame;
      last_sent_ = std::chrono::steady_clock::now();
    }
    sink_->OnFrame(std::move(frame), NowUs());
  }

  void SendLastFrame() {
    if (!repeat_) {
      return;
    }
    std::shared_ptr<VideoFrameBuffer> frame;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      frame = last_frame_;
      last_sent_ = std::chrono::steady_clock::now();
    }
    if (frame) {
      sink_->OnFrame(frame, NowUs());
    }
  }

 private:
  void Loop() {
    constexpr auto kIdleRepeat = std::chrono::milliseconds(500);
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stop_) {
      cv_.wait_for(lock, kIdleRepeat);
      if (stop_) {
        break;
      }
      if (repeat_ && last_frame_ &&
          std::chrono::steady_clock::now() - last_sent_ >= kIdleRepeat) {
        auto frame = last_frame_;
        last_sent_ = std::chrono::steady_clock::now();
        lock.unlock();
        sink_->OnFrame(frame, NowUs());
        lock.lock();
      }
    }
  }

  std::shared_ptr<VideoSink> sink_;
  bool repeat_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::shared_ptr<VideoFrameBuffer> last_frame_;
  std::chrono::steady_clock::time_point last_sent_;
  bool stop_ = false;
  std::thread thread_;
};

// Raw scrcpy audio (48kHz stereo s16le) cut into the 10ms chunks WebRTC wants.
class PcmChunker {
 public:
  static constexpr int kSampleRate = 48000;
  static constexpr int kChannels = 2;
  static constexpr int kBytesPerFrame = kChannels * 2;
  static constexpr int kFramesPerChunk = kSampleRate / 100;

  explicit PcmChunker(std::shared_ptr<AudioSink> sink)
      : sink_(std::move(sink)) {}

  void OnData(const uint8_t* data, size_t size) {
    pending_.insert(pending_.end(), data, data + size);
    constexpr size_t kChunkBytes = kFramesPerChunk * kBytesPerFrame;
    size_t offset = 0;
    while (pending_.size() - offset >= kChunkBytes) {
      Chunk chunk(pending_.data() + offset);
      sink_->OnFrame(chunk, NowUs());
      offset += kChunkBytes;
    }
    pending_.erase(pending_.begin(), pending_.begin() + offset);
  }

 private:
  class Chunk : public AudioFrameBuffer {
   public:
    explicit Chunk(const uint8_t* data) : data_(data) {}
    int bits_per_sample() const override { return 16; }
    int sample_rate() const override { return kSampleRate; }
    int channels() const override { return kChannels; }
    int frames() const override { return kFramesPerChunk; }
    const uint8_t* data() const override { return data_; }

   private:
    const uint8_t* data_;
  };

  std::shared_ptr<AudioSink> sink_;
  std::vector<uint8_t> pending_;
};

struct StreamerSetup {
  std::unique_ptr<cuttlefish::ClientFilesServer> client_server;
  std::unique_ptr<RecordingManager> recording_manager;
  std::unique_ptr<Streamer> streamer;
};

std::optional<StreamerSetup> CreateStreamer(
    std::shared_ptr<ConnectionObserverFactory> factory, int adb_port) {
  StreamerSetup setup;
  setup.client_server = cuttlefish::ClientFilesServer::New(FLAGS_client_dir);
  if (!setup.client_server) {
    LOG(ERROR) << "Failed to serve client files from " << FLAGS_client_dir;
    return std::nullopt;
  }
  StreamerConfig config;
  config.device_id = FLAGS_device_id;
  config.client_files_port = setup.client_server->port();
  config.operator_path = FLAGS_operator_path;
  config.udp_port_range = {static_cast<uint16_t>(FLAGS_udp_port_min),
                           static_cast<uint16_t>(FLAGS_udp_port_max)};
  config.tcp_port_range = config.udp_port_range;
  config.adb_port = adb_port;
  config.enable_mouse = false;
  config.enable_gamepad = false;

  setup.recording_manager = std::make_unique<RecordingManager>();
  setup.streamer = Streamer::Create(config, *setup.recording_manager, factory);
  if (!setup.streamer) {
    LOG(ERROR) << "Failed to create streamer";
    return std::nullopt;
  }
  return setup;
}

void Register(Streamer& streamer) {
  static auto operator_observer = std::make_shared<LoggingOperatorObserver>();
  streamer.Register(operator_observer);
  LOG(INFO) << "Device '" << FLAGS_device_id << "' registered via "
            << FLAGS_operator_path;
}

// ---------------------------------------------------------------- test pattern

class TestPattern {
 public:
  TestPattern(int width, int height)
      : width_(width), height_(height), pixels_(width * height * 4) {}

  std::shared_ptr<CvdAbgrVideoFrameBuffer> Render(int frame,
                                                  const PointerState& pointer) {
    static constexpr uint8_t kBars[8][3] = {
        {255, 255, 255}, {255, 255, 0}, {0, 255, 255}, {0, 255, 0},
        {255, 0, 255},   {255, 0, 0},   {0, 0, 255},   {16, 16, 16}};
    int bar_width = (width_ + 7) / 8;
    int shift = (frame * 4) % width_;
    for (int y = 0; y < height_; y++) {
      uint8_t* row = &pixels_[y * width_ * 4];
      for (int x = 0; x < width_; x++) {
        const uint8_t* c = kBars[((x + shift) % width_) / bar_width];
        row[x * 4 + 0] = c[0];
        row[x * 4 + 1] = c[1];
        row[x * 4 + 2] = c[2];
        row[x * 4 + 3] = 255;
      }
    }
    int px = pointer.x, py = pointer.y;
    if (px >= 0 && py >= 0) {
      int half = pointer.down ? 40 : 20;
      uint8_t v = pointer.down ? 255 : 200;
      for (int y = std::max(0, py - half); y < std::min(height_, py + half);
           y++) {
        for (int x = std::max(0, px - half); x < std::min(width_, px + half);
             x++) {
          uint8_t* p = &pixels_[(y * width_ + x) * 4];
          p[0] = p[1] = p[2] = v;
        }
      }
    }
    return std::make_shared<CvdAbgrVideoFrameBuffer>(
        width_, height_, DRM_FORMAT_ABGR8888, width_ * 4, pixels_.data());
  }

 private:
  int width_;
  int height_;
  std::vector<uint8_t> pixels_;
};

int RunTestPattern() {
  auto pointer = std::make_shared<PointerState>();
  auto setup = CreateStreamer(
      std::make_shared<LoggingConnectionObserverFactory>(pointer), 0);
  if (!setup) {
    return 1;
  }
  int dpi = FLAGS_dpi > 0 ? FLAGS_dpi : 320;
  auto display = setup->streamer->AddDisplay(kDisplayLabel, FLAGS_width,
                                             FLAGS_height, dpi, true);
  setup->streamer->SetHardwareSpec("Device", std::string("test pattern"));
  Register(*setup->streamer);

  TestPattern pattern(FLAGS_width, FLAGS_height);
  auto frame_interval = std::chrono::microseconds(1000000 / FLAGS_fps);
  auto next_frame = std::chrono::steady_clock::now();
  for (int frame = 0; g_running; frame++) {
    display->OnFrame(pattern.Render(frame, *pointer), NowUs());
    next_frame += frame_interval;
    std::this_thread::sleep_until(next_frame);
  }
  setup->streamer->Unregister();
  return 0;
}

// ---------------------------------------------------------------------- scrcpy

int ReadDpi(const Adb& adb) {
  if (FLAGS_dpi > 0) {
    return FLAGS_dpi;
  }
  for (const char* prop : {"ro.sf.lcd_density", "ro.boot.redroid_dpi"}) {
    auto value = adb.GetProp(prop);
    if (value && !value->empty()) {
      int dpi = atoi(value->c_str());
      if (dpi > 0) {
        return dpi;
      }
    }
  }
  return 320;
}

// Physical display size from `wm size` ("Physical size: 720x1280").
std::optional<std::pair<int, int>> ReadDisplaySize(const Adb& adb) {
  auto out = adb.Shell("wm size");
  if (!out) {
    return std::nullopt;
  }
  auto pos = out->find("Physical size: ");
  int width = 0, height = 0;
  if (pos == std::string::npos ||
      sscanf(out->c_str() + pos, "Physical size: %dx%d", &width, &height) !=
          2 ||
      width <= 0 || height <= 0) {
    return std::nullopt;
  }
  return std::make_pair(width, height);
}

int AdbPort(const std::string& serial) {
  auto colon = serial.rfind(':');
  return colon == std::string::npos ? 0 : atoi(serial.c_str() + colon + 1);
}

// Owns one scrcpy session plus its decoder and reports disconnections.
// Android applies the media volume to what scrcpy captures (the remote submix
// output), and the default index (5 of 15) attenuates it by ~33 dB. Raise it
// to the maximum the first time a device is streamed (remembered in a global
// setting on the device, which lives in /data like the volume), so later
// choices (volume buttons in the web UI) are kept across restarts. The shell
// user may not set volumes directly, so press volume up; while capturing, the
// volume keys adjust the remote submix device.
constexpr auto kCaptureVolumeSetting = "redroid_webrtc_capture_volume_set";

struct MusicVolume {
  int max = 15;
  int remote_submix = -1;  // -1: unknown
};

// Parses the STREAM_MUSIC section of `dumpsys audio`.
std::optional<MusicVolume> ReadMusicVolume(const Adb& adb) {
  auto dump = adb.Shell("dumpsys audio");
  if (!dump) {
    return std::nullopt;
  }
  auto begin = dump->find("- STREAM_MUSIC:");
  if (begin == std::string::npos) {
    return std::nullopt;
  }
  auto end = dump->find("\n- STREAM_", begin + 1);
  std::string section = dump->substr(
      begin, end == std::string::npos ? std::string::npos : end - begin);
  MusicVolume volume;
  if (auto pos = section.find("Max: "); pos != std::string::npos) {
    volume.max = std::clamp(atoi(section.c_str() + pos + 5), 1, 100);
  }
  constexpr std::string_view kSubmix = "(remote_submix): ";
  if (auto pos = section.find(kSubmix); pos != std::string::npos) {
    volume.remote_submix = atoi(section.c_str() + pos + kSubmix.size());
  }
  return volume;
}

void RaiseCaptureVolumeOnce(const Adb& adb, DeviceInput& input) {
  auto marker =
      adb.Shell(std::string("settings get global ") + kCaptureVolumeSetting);
  if (!marker || marker->rfind("1", 0) == 0) {
    return;
  }
  for (int attempt = 0; attempt < 3 && g_running; attempt++) {
    auto volume = ReadMusicVolume(adb);
    if (!volume) {
      return;
    }
    if (volume->remote_submix >= volume->max) {
      adb.Shell(std::string("settings put global ") + kCaptureVolumeSetting +
                " 1");
      LOG(INFO) << "Captured media volume is at its maximum (" << volume->max
                << ")";
      return;
    }
    LOG(INFO) << "Raising the captured media volume from "
              << volume->remote_submix << " to " << volume->max
              << " (first stream of this device)";
    // One extra press: the first one may only show the volume panel.
    for (int i = 0; i <= volume->max; i++) {
      for (uint8_t action : {scrcpy::kActionDown, scrcpy::kActionUp}) {
        input.Send(
            scrcpy::InjectKeycode(action, kAndroidKeycodeVolumeUp, 0, 0));
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  LOG(WARNING) << "Could not raise the captured media volume";
}

class DeviceConnection {
 public:
  DeviceConnection(const Adb& adb, std::shared_ptr<DeviceInput> input)
      : adb_(adb), input_(std::move(input)) {}

  // probe: short-lived session used only to read the stream properties.
  bool Connect(FrameRepeater* repeater, PcmChunker* audio, bool probe = false) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      disconnected_ = false;
    }
    auto on_frame = [this, repeater](std::shared_ptr<VideoFrameBuffer> frame) {
      // The device restarts its encoder with new dimensions on rotation. The
      // browser scales touches to the frame size, and the scrcpy server drops
      // events whose screen size differs from the current video size, so
      // follow the frames.
      if (frame->width() != input_->width() ||
          frame->height() != input_->height()) {
        LOG(INFO) << "Video size is now " << frame->width() << "x"
                  << frame->height();
        input_->SetScreenSize(frame->width(), frame->height());
      }
      if (repeater) {
        repeater->OnFrame(std::move(frame));
      }
    };
    bool hwc = FLAGS_source == "hwc";
    if (hwc) {
      // Frames come from the hwcomposer socket, not from scrcpy.
    } else if (FLAGS_video_passthrough) {
      assembler_ = std::make_unique<H264FrameAssembler>(on_frame);
    } else {
      decoder_ = H264Decoder::Create(on_frame);
      if (!decoder_) {
        return false;
      }
    }
    ScrcpySession::Options options;
    options.server_path = FLAGS_scrcpy_server;
    options.server_version = FLAGS_scrcpy_version;
    options.local_port = FLAGS_scrcpy_port;
    options.max_fps = FLAGS_fps;
    options.video_bit_rate = FLAGS_video_bit_rate;
    options.audio = FLAGS_audio;
    options.video = !hwc;
    ScrcpySession::Callbacks callbacks;
    callbacks.on_video_packet = [this](const scrcpy::PacketHeader& header,
                                       const uint8_t* data, size_t size) {
      if (assembler_) {
        assembler_->OnPacket(data, size, header.config, header.key_frame);
      } else {
        decoder_->Decode(data, size, header.config);
      }
    };
    callbacks.on_audio_packet = [audio](const uint8_t* data, size_t size) {
      if (audio) {
        audio->OnData(data, size);
      }
    };
    callbacks.on_disconnected = [this] {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        disconnected_ = true;
      }
      cv_.notify_all();
    };
    session_ = std::make_unique<ScrcpySession>(adb_, options, callbacks);
    if (!session_->Start()) {
      session_.reset();
      return false;
    }
    input_->SetScreenSize(session_->video_header().width,
                          session_->video_header().height);
    input_->SetSender(
        [session = session_.get()](const std::vector<uint8_t>& msg) {
          return session->SendControl(msg);
        });
    if (!probe && FLAGS_raise_capture_volume && session_->audio_enabled()) {
      RaiseCaptureVolumeOnce(adb_, *input_);
    }
    return true;
  }

  void WaitForDisconnect() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return disconnected_ || !g_running; });
  }

  void Interrupt() { cv_.notify_all(); }

  void Disconnect() {
    input_->SetSender(nullptr);
    session_.reset();
    decoder_.reset();
    assembler_.reset();
  }

  const ScrcpySession* session() const { return session_.get(); }

 private:
  const Adb& adb_;
  std::shared_ptr<DeviceInput> input_;
  std::unique_ptr<H264Decoder> decoder_;
  std::unique_ptr<H264FrameAssembler> assembler_;
  std::unique_ptr<ScrcpySession> session_;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool disconnected_ = false;
};

DeviceConnection* g_connection = nullptr;

bool WaitForDevice(const Adb& adb) {
  LOG(INFO) << "Waiting for adb device " << adb.serial();
  auto timeout = std::chrono::seconds(FLAGS_boot_timeout_s);
  if (!adb.Connect(timeout)) {
    LOG(ERROR) << "Could not connect to " << adb.serial();
    return false;
  }
  if (!adb.WaitForBoot(timeout)) {
    LOG(ERROR) << adb.serial() << " did not finish booting";
    return false;
  }
  return true;
}

int RunScrcpy() {
  Adb adb(FLAGS_adb, FLAGS_adb_serial);
  adb.set_cancelled_check([] { return !g_running; });
  if (!WaitForDevice(adb)) {
    return 1;
  }
  int dpi = ReadDpi(adb);

  auto input = std::make_shared<DeviceInput>();
  DeviceConnection connection(adb, input);
  g_connection = &connection;

  // The first session tells us the display size and whether audio works,
  // which are needed to create the WebRTC tracks before any frame flows.
  if (!connection.Connect(nullptr, nullptr, /*probe=*/true)) {
    LOG(ERROR) << "Failed to start scrcpy on " << adb.serial();
    return 1;
  }
  int width = connection.session()->video_header().width;
  int height = connection.session()->video_header().height;
  if (FLAGS_source == "hwc") {
    auto size = ReadDisplaySize(adb);
    if (!size) {
      LOG(ERROR) << "Could not read the display size of " << adb.serial();
      return 1;
    }
    std::tie(width, height) = *size;
    input->SetScreenSize(width, height);
  }
  std::string device_name = connection.session()->device_name();
  bool has_audio = connection.session()->audio_enabled();
  connection.Disconnect();

  if (FLAGS_video_passthrough) {
    RegisterH264PassthroughEncoder(
        [input] { input->Send(scrcpy::ResetVideo()); });
  }
  auto setup =
      CreateStreamer(std::make_shared<RedroidConnectionObserverFactory>(
                         input, FLAGS_adb_serial),
                     AdbPort(FLAGS_adb_serial));
  if (!setup) {
    return 1;
  }
  auto display = setup->streamer->AddDisplay(kDisplayLabel, width, height, dpi,
                                             /*touch_enabled=*/true);
  if (!display) {
    LOG(ERROR) << "Failed to add display";
    return 1;
  }
  std::unique_ptr<PcmChunker> audio;
  if (has_audio) {
    audio = std::make_unique<PcmChunker>(
        setup->streamer->AddAudioStream(kAudioLabel));
  }
  std::unique_ptr<MicInjector> mic;
  if (!FLAGS_mic_helper.empty()) {
    mic = std::make_unique<MicInjector>(adb, FLAGS_mic_helper,
                                        setup->streamer->GetAudioSource());
    mic->Start();
  }
  setup->streamer->SetHardwareSpec("Device", device_name);
  setup->streamer->SetHardwareSpec("adb", FLAGS_adb_serial);
  FrameRepeater repeater(display, /*repeat=*/!FLAGS_video_passthrough);
  input->on_rotate = [&adb](int quarter_turns) {
    int current = 0;
    if (auto out = adb.Shell("dumpsys window displays 2>/dev/null | grep "
                             "-m1 mCurrentRotation")) {
      auto pos = out->find("ROTATION_");
      if (pos != std::string::npos) {
        current = atoi(out->c_str() + pos + 9) / 90;
      }
    }
    int target = (current + quarter_turns) % 4;
    LOG(INFO) << "Rotating display to " << target * 90 << " degrees";
    g_fast_rotation_polls = 15;
    adb.Shell(
        "settings put system accelerometer_rotation 0 && "
        "settings put system user_rotation " +
        std::to_string(target));
  };
  input->on_client_connected = [&repeater, input] {
    if (FLAGS_video_passthrough) {
      // New clients need a key frame, and an idle screen sends no frames.
      input->Send(scrcpy::ResetVideo());
    } else {
      repeater.SendLastFrame();
    }
  };
  Register(*setup->streamer);
  LOG(INFO) << "Streaming '" << device_name << "' " << width << "x" << height
            << " dpi " << dpi << (has_audio ? " with audio" : " without audio");

  std::unique_ptr<HwcFrameSource> hwc_source;
  std::thread rotation_poller;
  if (FLAGS_source == "hwc") {
    hwc_source = std::make_unique<HwcFrameSource>(
        FLAGS_hwc_socket, width, height,
        [&repeater, input](std::shared_ptr<VideoFrameBuffer> frame) {
          // Rotated frames swap dimensions; touches are in frame coordinates
          // which match the device's logical (rotated) display.
          if (frame->width() != input->width() ||
              frame->height() != input->height()) {
            LOG(INFO) << "Video size is now " << frame->width() << "x"
                      << frame->height();
            input->SetScreenSize(frame->width(), frame->height());
          }
          repeater.OnFrame(std::move(frame));
        });
    hwc_source->Start();
    // The hwcomposer stream carries no orientation: poll it.
    rotation_poller = std::thread([&adb, source = hwc_source.get()] {
      while (g_running) {
        if (auto out = adb.Shell("dumpsys window displays 2>/dev/null | grep "
                                 "-m1 mCurrentRotation")) {
          auto pos = out->find("ROTATION_");
          if (pos != std::string::npos) {
            source->set_rotation(atoi(out->c_str() + pos + 9));
          }
        }
        // Poll every 200ms for a while after a rotation request, so frames
        // are not sent sideways until the next regular (1s) poll.
        if (g_fast_rotation_polls > 0) {
          g_fast_rotation_polls--;
          std::this_thread::sleep_for(std::chrono::milliseconds(200));
          continue;
        }
        for (int i = 0; i < 5 && g_running && g_fast_rotation_polls == 0; i++) {
          std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
      }
    });
  }

  auto backoff = std::chrono::seconds(1);
  while (g_running) {
    if (connection.Connect(&repeater, audio.get())) {
      backoff = std::chrono::seconds(1);
      const auto& header = connection.session()->video_header();
      if (static_cast<int>(header.width) != width ||
          static_cast<int>(header.height) != height) {
        LOG(WARNING) << "Device resolution changed to " << header.width << "x"
                     << header.height << " (display created as " << width << "x"
                     << height << ")";
      }
      connection.WaitForDisconnect();
      connection.Disconnect();
    }
    if (!g_running) {
      break;
    }
    LOG(WARNING) << "Reconnecting to " << adb.serial() << " in "
                 << backoff.count() << "s";
    auto wake = std::chrono::steady_clock::now() + backoff;
    while (g_running && std::chrono::steady_clock::now() < wake) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    backoff = std::min(backoff * 2, std::chrono::seconds(30));
    WaitForDevice(adb);
  }

  if (rotation_poller.joinable()) {
    rotation_poller.join();
  }
  mic.reset();
  setup->streamer->Unregister();
  return 0;
}

}  // namespace
}  // namespace redroid

int main(int argc, char** argv) {
  absl::InitializeLog();
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  auto on_signal = [](int) {
    redroid::g_running = false;
    if (redroid::g_connection) {
      redroid::g_connection->Interrupt();
    }
  };
  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);
  signal(SIGPIPE, SIG_IGN);
  if (FLAGS_source == "test_pattern") {
    return redroid::RunTestPattern();
  }
  if (FLAGS_source == "hwc" && FLAGS_hwc_socket.empty()) {
    LOG(ERROR) << "--source=hwc needs --hwc_socket";
    return 1;
  }
  if (FLAGS_source == "hwc" && FLAGS_video_passthrough) {
    LOG(ERROR) << "--video_passthrough needs --source=scrcpy";
    return 1;
  }
  if (FLAGS_source != "scrcpy" && FLAGS_source != "hwc") {
    LOG(ERROR) << "Unknown --source " << FLAGS_source;
    return 1;
  }
  return redroid::RunScrcpy();
}
