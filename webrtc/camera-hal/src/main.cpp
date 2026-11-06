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

// Camera provider fed with a browser's webcam. The streamer serves I420 frames
// on a unix socket while a camera session is open; without frames, the camera
// shows a moving test pattern. Built with the NDK, see build.sh.

#include <aidl/android/hardware/camera/common/Status.h>
#include <aidl/android/hardware/camera/device/BnCameraDevice.h>
#include <aidl/android/hardware/camera/device/BnCameraDeviceSession.h>
#include <aidl/android/hardware/camera/device/CameraBlob.h>
#include <aidl/android/hardware/camera/device/CameraBlobId.h>
#include <aidl/android/hardware/camera/provider/BnCameraProvider.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android/hardware_buffer.h>
#include <android/log.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <system/camera_metadata.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

using namespace aidl::android::hardware::camera;
using namespace aidl::android::hardware::graphics::common;
using ndk::ScopedAStatus;
using ndk::SharedRefBase;
using common::Status;
using Clock = std::chrono::steady_clock;

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "redroid-cam", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "redroid-cam", __VA_ARGS__)

extern "C" {
struct native_handle {
  int version, numFds, numInts;
  int data[0];
};
typedef struct native_handle native_handle_t;
native_handle_t* native_handle_create(int numFds, int numInts);
int native_handle_close(const native_handle_t*);
int native_handle_delete(native_handle_t*);
int AHardwareBuffer_createFromHandle(const AHardwareBuffer_Desc*, const native_handle_t*,
                                     int32_t method, AHardwareBuffer**);
}

namespace {

constexpr int kWidth = 1280, kHeight = 720;  // Socket frame and largest stream size.
constexpr int kFrameBytes = kWidth * kHeight * 3 / 2;
constexpr int kFormatBlob = 0x21, kFormatImpl = 0x22, kFormatYuv = 0x23;
constexpr int kJpegMaxSize = 3 * 1024 * 1024;
constexpr int64_t kFrameNs = 33333333;
constexpr char kSocket[] = "/ipc/camera.sock";
const std::vector<std::pair<int, int>> kSizes = {{1280, 720}, {640, 480}, {320, 240}};

ScopedAStatus Err(Status s) { return ScopedAStatus::fromServiceSpecificError(static_cast<int>(s)); }

// Small camera_metadata_t builder.
class Meta {
 public:
  explicit Meta(size_t entries = 96, size_t data = 8192)
      : m_(allocate_camera_metadata(entries, data)) {}
  explicit Meta(const std::vector<uint8_t>& settings) {
    auto* src = reinterpret_cast<const camera_metadata_t*>(settings.data());
    m_ = allocate_camera_metadata(get_camera_metadata_entry_count(src) + 24,
                                  get_camera_metadata_data_count(src) + 512);
    append_camera_metadata(m_, src);
  }
  ~Meta() { free_camera_metadata(m_); }
  Meta(const Meta&) = delete;

  template <typename T>
  void Set(uint32_t tag, std::vector<T> v) {
    camera_metadata_entry_t entry;
    if (find_camera_metadata_entry(m_, tag, &entry) == 0) {
      update_camera_metadata_entry(m_, entry.index, v.data(), v.size(), nullptr);
    } else {
      add_camera_metadata_entry(m_, tag, v.data(), v.size());
    }
  }
  void U8(uint32_t tag, uint8_t v) { Set<uint8_t>(tag, {v}); }
  void I32(uint32_t tag, std::vector<int32_t> v) { Set(tag, std::move(v)); }
  void I64(uint32_t tag, std::vector<int64_t> v) { Set(tag, std::move(v)); }
  void F(uint32_t tag, float v) { Set<float>(tag, {v}); }

  std::vector<int32_t> Tags() const {
    std::vector<int32_t> tags;
    for (size_t i = 0; i < get_camera_metadata_entry_count(m_); i++) {
      camera_metadata_ro_entry_t e;
      get_camera_metadata_ro_entry(m_, i, &e);
      tags.push_back(e.tag);
    }
    return tags;
  }
  std::vector<uint8_t> Bytes() {
    sort_camera_metadata(m_);
    auto* p = reinterpret_cast<uint8_t*>(m_);
    return std::vector<uint8_t>(p, p + get_camera_metadata_size(m_));
  }

 private:
  camera_metadata_t* m_;
};

std::vector<uint8_t> Characteristics() {
  Meta m;
  const std::vector<int32_t> request_keys = {
      ANDROID_COLOR_CORRECTION_ABERRATION_MODE, ANDROID_CONTROL_AE_ANTIBANDING_MODE,
      ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, ANDROID_CONTROL_AE_LOCK, ANDROID_CONTROL_AE_MODE,
      ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER, ANDROID_CONTROL_AE_TARGET_FPS_RANGE,
      ANDROID_CONTROL_AF_MODE, ANDROID_CONTROL_AF_TRIGGER, ANDROID_CONTROL_AWB_LOCK,
      ANDROID_CONTROL_AWB_MODE, ANDROID_CONTROL_CAPTURE_INTENT, ANDROID_CONTROL_EFFECT_MODE,
      ANDROID_CONTROL_MODE, ANDROID_CONTROL_SCENE_MODE, ANDROID_CONTROL_VIDEO_STABILIZATION_MODE,
      ANDROID_FLASH_MODE, ANDROID_JPEG_ORIENTATION, ANDROID_JPEG_QUALITY,
      ANDROID_JPEG_THUMBNAIL_QUALITY, ANDROID_JPEG_THUMBNAIL_SIZE, ANDROID_NOISE_REDUCTION_MODE,
      ANDROID_SCALER_CROP_REGION, ANDROID_STATISTICS_FACE_DETECT_MODE};
  std::vector<int32_t> result_keys = request_keys;
  result_keys.insert(result_keys.end(),
                     {ANDROID_CONTROL_AE_STATE, ANDROID_CONTROL_AF_STATE,
                      ANDROID_CONTROL_AWB_STATE, ANDROID_FLASH_STATE, ANDROID_LENS_STATE,
                      ANDROID_REQUEST_PIPELINE_DEPTH, ANDROID_SENSOR_TIMESTAMP,
                      ANDROID_SENSOR_ROLLING_SHUTTER_SKEW});

  m.U8(ANDROID_COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES,
       ANDROID_COLOR_CORRECTION_ABERRATION_MODE_OFF);
  m.U8(ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES, ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO);
  m.U8(ANDROID_CONTROL_AE_AVAILABLE_MODES, ANDROID_CONTROL_AE_MODE_ON);
  m.I32(ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES, {15, 30, 30, 30});
  m.I32(ANDROID_CONTROL_AE_COMPENSATION_RANGE, {0, 0});
  m.Set<camera_metadata_rational_t>(ANDROID_CONTROL_AE_COMPENSATION_STEP, {{1, 1}});
  m.U8(ANDROID_CONTROL_AE_LOCK_AVAILABLE, 0);
  m.U8(ANDROID_CONTROL_AF_AVAILABLE_MODES, ANDROID_CONTROL_AF_MODE_OFF);
  m.U8(ANDROID_CONTROL_AVAILABLE_EFFECTS, ANDROID_CONTROL_EFFECT_MODE_OFF);
  m.U8(ANDROID_CONTROL_AVAILABLE_MODES, ANDROID_CONTROL_MODE_AUTO);
  m.U8(ANDROID_CONTROL_AVAILABLE_SCENE_MODES, ANDROID_CONTROL_SCENE_MODE_DISABLED);
  m.U8(ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES,
       ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF);
  m.U8(ANDROID_CONTROL_AWB_AVAILABLE_MODES, ANDROID_CONTROL_AWB_MODE_AUTO);
  m.U8(ANDROID_CONTROL_AWB_LOCK_AVAILABLE, 0);
  m.I32(ANDROID_CONTROL_MAX_REGIONS, {0, 0, 0});
  m.U8(ANDROID_EDGE_AVAILABLE_EDGE_MODES, ANDROID_EDGE_MODE_OFF);
  m.U8(ANDROID_FLASH_INFO_AVAILABLE, 0);
  m.U8(ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL, ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_EXTERNAL);
  m.I32(ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES, {0, 0, 160, 90, 160, 120});
  m.I32(ANDROID_JPEG_MAX_SIZE, {kJpegMaxSize});
  m.U8(ANDROID_LENS_FACING, ANDROID_LENS_FACING_FRONT);
  m.F(ANDROID_LENS_INFO_AVAILABLE_APERTURES, 2.8f);
  m.F(ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS, 3.0f);
  m.F(ANDROID_LENS_INFO_AVAILABLE_FILTER_DENSITIES, 0.0f);
  m.U8(ANDROID_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION,
       ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF);
  m.U8(ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION,
       ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION_UNCALIBRATED);
  m.F(ANDROID_LENS_INFO_HYPERFOCAL_DISTANCE, 0.0f);
  m.F(ANDROID_LENS_INFO_MINIMUM_FOCUS_DISTANCE, 0.0f);
  m.I32(ANDROID_LENS_INFO_SHADING_MAP_SIZE, {1, 1});
  m.U8(ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES, ANDROID_NOISE_REDUCTION_MODE_OFF);
  m.U8(ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
       ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
  m.I32(ANDROID_REQUEST_MAX_NUM_INPUT_STREAMS, {0});
  m.I32(ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS, {0, 2, 1});
  m.I32(ANDROID_REQUEST_PARTIAL_RESULT_COUNT, {1});
  m.U8(ANDROID_REQUEST_PIPELINE_MAX_DEPTH, 4);
  m.F(ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM, 1.0f);
  m.U8(ANDROID_SCALER_CROPPING_TYPE, ANDROID_SCALER_CROPPING_TYPE_CENTER_ONLY);
  std::vector<int32_t> configs;
  std::vector<int64_t> durations, stalls;
  for (auto [w, h] : kSizes) {
    for (int format : {kFormatImpl, kFormatYuv, kFormatBlob}) {
      configs.insert(configs.end(),
                     {format, w, h, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT});
      durations.insert(durations.end(), {format, w, h, kFrameNs});
    }
    stalls.insert(stalls.end(), {kFormatBlob, w, h, kFrameNs});
  }
  m.I32(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, configs);
  m.I64(ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS, durations);
  m.I64(ANDROID_SCALER_AVAILABLE_STALL_DURATIONS, stalls);
  m.I32(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE, {0, 0, kWidth, kHeight});
  m.I32(ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE, {kWidth, kHeight});
  m.I32(ANDROID_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE, {0, 0, kWidth, kHeight});
  m.Set<float>(ANDROID_SENSOR_INFO_PHYSICAL_SIZE, {3.2f, 1.8f});
  m.U8(ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE, ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE_UNKNOWN);
  m.I32(ANDROID_SENSOR_ORIENTATION, {270});
  m.U8(ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES,
       ANDROID_STATISTICS_FACE_DETECT_MODE_OFF);
  m.I32(ANDROID_STATISTICS_INFO_MAX_FACE_COUNT, {0});
  m.I32(ANDROID_SYNC_MAX_LATENCY, {ANDROID_SYNC_MAX_LATENCY_UNKNOWN});
  m.I32(ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS, request_keys);
  m.I32(ANDROID_REQUEST_AVAILABLE_RESULT_KEYS, result_keys);
  auto tags = m.Tags();
  tags.push_back(ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS);
  m.I32(ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS, tags);
  return m.Bytes();
}

std::vector<uint8_t> DefaultRequest(uint8_t intent) {
  Meta m;
  m.U8(ANDROID_COLOR_CORRECTION_ABERRATION_MODE, ANDROID_COLOR_CORRECTION_ABERRATION_MODE_OFF);
  m.U8(ANDROID_CONTROL_AE_ANTIBANDING_MODE, ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO);
  m.I32(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, {0});
  m.U8(ANDROID_CONTROL_AE_LOCK, 0);
  m.U8(ANDROID_CONTROL_AE_MODE, ANDROID_CONTROL_AE_MODE_ON);
  m.U8(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER, ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_IDLE);
  m.I32(ANDROID_CONTROL_AE_TARGET_FPS_RANGE,
        {intent == ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_RECORD ? 30 : 15, 30});
  m.U8(ANDROID_CONTROL_AF_MODE, ANDROID_CONTROL_AF_MODE_OFF);
  m.U8(ANDROID_CONTROL_AF_TRIGGER, ANDROID_CONTROL_AF_TRIGGER_IDLE);
  m.U8(ANDROID_CONTROL_AWB_LOCK, 0);
  m.U8(ANDROID_CONTROL_AWB_MODE, ANDROID_CONTROL_AWB_MODE_AUTO);
  m.U8(ANDROID_CONTROL_CAPTURE_INTENT, intent);
  m.U8(ANDROID_CONTROL_EFFECT_MODE, ANDROID_CONTROL_EFFECT_MODE_OFF);
  m.U8(ANDROID_CONTROL_MODE, ANDROID_CONTROL_MODE_AUTO);
  m.U8(ANDROID_CONTROL_SCENE_MODE, ANDROID_CONTROL_SCENE_MODE_DISABLED);
  m.U8(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE, ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF);
  m.U8(ANDROID_FLASH_MODE, ANDROID_FLASH_MODE_OFF);
  m.I32(ANDROID_JPEG_ORIENTATION, {0});
  m.U8(ANDROID_JPEG_QUALITY, 90);
  m.U8(ANDROID_JPEG_THUMBNAIL_QUALITY, 80);
  m.I32(ANDROID_JPEG_THUMBNAIL_SIZE, {0, 0});
  m.U8(ANDROID_NOISE_REDUCTION_MODE, ANDROID_NOISE_REDUCTION_MODE_OFF);
  m.I32(ANDROID_SCALER_CROP_REGION, {0, 0, kWidth, kHeight});
  m.U8(ANDROID_STATISTICS_FACE_DETECT_MODE, ANDROID_STATISTICS_FACE_DETECT_MODE_OFF);
  return m.Bytes();
}

int JpegOrientation(const std::vector<uint8_t>& settings) {
  camera_metadata_ro_entry_t entry;
  if (settings.empty() ||
      find_camera_metadata_ro_entry(reinterpret_cast<const camera_metadata_t*>(settings.data()),
                                    ANDROID_JPEG_ORIENTATION, &entry) != 0 ||
      entry.count != 1) {
    return 0;
  }
  return entry.data.i32[0];
}

// Latest webcam frame. Connects to the streamer while a session is open.
class Webcam {
 public:
  Webcam() : thread_([this] { Loop(); }) {}
  ~Webcam() {
    running_ = false;
    thread_.join();
  }
  // Copies the frame if a recent one exists.
  bool Get(std::vector<uint8_t>* out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (frame_.empty() || Clock::now() - time_ > std::chrono::seconds(1)) {
      return false;
    }
    *out = frame_;
    return true;
  }


 private:
  bool ReadAll(int fd, uint8_t* p, size_t n) {
    while (n > 0 && running_) {
      pollfd pfd{fd, POLLIN, 0};
      if (poll(&pfd, 1, 200) <= 0) {
        continue;
      }
      ssize_t got = read(fd, p, n);
      if (got <= 0) {
        return false;
      }
      p += got;
      n -= got;
    }
    return n == 0;
  }
  void Loop() {
    std::vector<uint8_t> buffer(kFrameBytes);
    while (running_) {
      int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
      sockaddr_un addr{};
      addr.sun_family = AF_UNIX;
      strncpy(addr.sun_path, kSocket, sizeof(addr.sun_path) - 1);
      if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(fd);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        continue;
      }
      LOGI("webcam connected");
      while (running_ && ReadAll(fd, buffer.data(), buffer.size())) {
        std::lock_guard<std::mutex> lock(mutex_);
        frame_.swap(buffer);
        buffer.resize(kFrameBytes);
        time_ = Clock::now();
      }
      close(fd);
      LOGI("webcam disconnected");
    }
  }
  std::atomic<bool> running_{true};
  std::mutex mutex_;
  std::vector<uint8_t> frame_;
  Clock::time_point time_;
  std::thread thread_;
};

// Fills `frame` with a test pattern: a grey field and a moving white bar.
void TestPattern(std::vector<uint8_t>* frame, int count) {
  frame->resize(kFrameBytes);
  memset(frame->data(), 90, kWidth * kHeight);
  memset(frame->data() + kWidth * kHeight, 128, kWidth * kHeight / 2);
  int x = (count * 8) % (kWidth - 64);
  for (int y = 0; y < kHeight; y++) {
    memset(frame->data() + y * kWidth + x, 235, 64);
  }
}

// Nearest neighbour lookup into a kWidth x kHeight source. Android expects a
// phone's sensor, mounted sideways (ANDROID_SENSOR_ORIENTATION 270), so output
// frames hold the centre of the scene rotated by 90 degrees.
struct Mapper {
  int crop_w, crop_h, x0, y0, w, h;
  Mapper(int width, int height) : w(width), h(height) {
    crop_w = std::min(kWidth, kHeight * h / w);
    crop_h = std::min(kHeight, kWidth * w / h);
    x0 = (kWidth - crop_w) / 2;
    y0 = (kHeight - crop_h) / 2;
  }
  int X(int, int y) const { return x0 + y * crop_w / h; }
  int Y(int x, int) const { return y0 + (w - 1 - x) * crop_h / w; }
};

void WriteYuv(const std::vector<uint8_t>& src, int w, int h, const AHardwareBuffer_Planes& planes) {
  Mapper map(w, h);
  const uint8_t* sy = src.data();
  const uint8_t* su = sy + kWidth * kHeight;
  const uint8_t* sv = su + kWidth * kHeight / 4;
  const auto& py = planes.planes[0];
  const auto& pu = planes.planes[1];
  const auto& pv = planes.planes[2];
  for (int y = 0; y < h; y++) {
    uint8_t* dst = static_cast<uint8_t*>(py.data) + y * py.rowStride;
    for (int x = 0; x < w; x++) {
      dst[x * py.pixelStride] = sy[map.Y(x, y) * kWidth + map.X(x, y)];
    }
  }
  for (int y = 0; y < h / 2; y++) {
    uint8_t* du = static_cast<uint8_t*>(pu.data) + y * pu.rowStride;
    uint8_t* dv = static_cast<uint8_t*>(pv.data) + y * pv.rowStride;
    for (int x = 0; x < w / 2; x++) {
      int offset = map.Y(x * 2, y * 2) / 2 * (kWidth / 2) + map.X(x * 2, y * 2) / 2;
      du[x * pu.pixelStride] = su[offset];
      dv[x * pv.pixelStride] = sv[offset];
    }
  }
}

// Converts to packed RGB (pixel 3) or RGBA (pixel 4) rows.
void WriteRgb(const std::vector<uint8_t>& src, int w, int h, uint8_t* rgb, int pixel, int stride) {
  Mapper map(w, h);
  const uint8_t* su = src.data() + kWidth * kHeight;
  const uint8_t* sv = su + kWidth * kHeight / 4;
  auto clamp = [](int v) { return static_cast<uint8_t>(std::clamp(v, 0, 255)); };
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      int xx = map.X(x, y), yy = map.Y(x, y);
      int luma = src[yy * kWidth + xx];
      int u = su[(yy / 2) * (kWidth / 2) + xx / 2] - 128;
      int v = sv[(yy / 2) * (kWidth / 2) + xx / 2] - 128;
      uint8_t* p = rgb + y * stride + x * pixel;
      p[0] = clamp(luma + (359 * v >> 8));
      p[1] = clamp(luma - ((88 * u + 183 * v) >> 8));
      p[2] = clamp(luma + (454 * u >> 8));
      if (pixel == 4) {
        p[3] = 255;
      }
    }
  }
}

// Writes the JPEG Android asked for, turned clockwise by `orientation` degrees.
// Like most phones, photos aren't mirrored, unlike the front camera's preview.
void WriteJpeg(const std::vector<uint8_t>& src, int w, int h, int orientation, uint8_t* out,
               int capacity) {
  std::vector<uint8_t> rgb(w * h * 3);
  WriteRgb(src, w, h, rgb.data(), 3, w * 3);
  bool swapped = orientation == 90 || orientation == 270;
  int out_w = swapped ? h : w, out_h = swapped ? w : h;
  std::vector<uint8_t> turned(out_w * out_h * 3);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      int tx = x, ty = y;
      if (orientation == 90) {
        tx = h - 1 - y;
        ty = x;
      } else if (orientation == 180) {
        tx = w - 1 - x;
        ty = h - 1 - y;
      } else if (orientation == 270) {
        tx = y;
        ty = w - 1 - x;
      }
      memcpy(&turned[(ty * out_w + tx) * 3], &rgb[(y * w + x) * 3], 3);
    }
  }
  struct Sink {
    uint8_t* p;
    int size, capacity;
  } sink{out, 0, capacity - static_cast<int>(sizeof(device::CameraBlob))};
  stbi_write_jpg_to_func(
      [](void* ctx, void* data, int size) {
        auto* s = static_cast<Sink*>(ctx);
        size = std::min(size, s->capacity - s->size);
        memcpy(s->p + s->size, data, size);
        s->size += size;
      },
      &sink, out_w, out_h, 3, turned.data(), 90);
  // Consumers find the JPEG's size in a trailer at the very end of the buffer.
  int32_t blob[2] = {static_cast<int32_t>(device::CameraBlobId::JPEG), sink.size};
  memcpy(out + capacity - sizeof(blob), blob, sizeof(blob));
}

class Session : public device::BnCameraDeviceSession {
 public:
  explicit Session(std::shared_ptr<device::ICameraDeviceCallback> callback)
      : callback_(std::move(callback)), worker_([this] { Worker(); }) {}
  ~Session() override { Stop(); }

  ScopedAStatus close() override {
    Stop();
    return ScopedAStatus::ok();
  }

  ScopedAStatus configureStreams(const device::StreamConfiguration& config,
                                 std::vector<device::HalStream>* out) override {
    std::map<int, device::Stream> streams;
    out->clear();
    for (const auto& s : config.streams) {
      int format = static_cast<int>(s.format);
      bool known_size = std::find(kSizes.begin(), kSizes.end(),
                                  std::make_pair(s.width, s.height)) != kSizes.end();
      if (s.streamType != device::StreamType::OUTPUT || !known_size ||
          (format != kFormatImpl && format != kFormatYuv && format != kFormatBlob)) {
        LOGE("unsupported stream %dx%d format %#x", s.width, s.height, format);
        return Err(Status::ILLEGAL_ARGUMENT);
      }
      device::HalStream hal;
      hal.id = s.id;
      // Preview and video: the software GPU can sample RGBA, not this YUV.
      hal.overrideFormat = format == kFormatImpl ? PixelFormat::RGBA_8888 : s.format;
      hal.producerUsage = BufferUsage::CPU_WRITE_OFTEN;
      hal.consumerUsage = static_cast<BufferUsage>(0);
      hal.maxBuffers = 4;
      hal.overrideDataSpace = s.dataSpace;
      out->push_back(hal);
      streams[s.id] = s;
      streams[s.id].format = hal.overrideFormat;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    streams_ = std::move(streams);
    return ScopedAStatus::ok();
  }

  ScopedAStatus constructDefaultRequestSettings(device::RequestTemplate type,
                                                device::CameraMetadata* out) override {
    int intent = static_cast<int>(type);
    if (intent < 1 || intent > 4) {
      return Err(Status::ILLEGAL_ARGUMENT);
    }
    out->metadata = DefaultRequest(intent);
    return ScopedAStatus::ok();
  }

  ScopedAStatus flush() override {
    std::unique_lock<std::mutex> lock(mutex_);
    flushing_ = true;
    wake_.notify_all();
    idle_.wait(lock, [this] { return queue_.empty() && !busy_; });
    flushing_ = false;
    return ScopedAStatus::ok();
  }

  // Empty descriptors: metadata then travels through binder.
  ScopedAStatus getCaptureRequestMetadataQueue(
      aidl::android::hardware::common::fmq::MQDescriptor<
          int8_t, aidl::android::hardware::common::fmq::SynchronizedReadWrite>*) override {
    return ScopedAStatus::ok();
  }
  ScopedAStatus getCaptureResultMetadataQueue(
      aidl::android::hardware::common::fmq::MQDescriptor<
          int8_t, aidl::android::hardware::common::fmq::SynchronizedReadWrite>*) override {
    return ScopedAStatus::ok();
  }
  ScopedAStatus isReconfigurationRequired(const device::CameraMetadata&,
                                          const device::CameraMetadata&, bool* out) override {
    *out = false;
    return ScopedAStatus::ok();
  }

  ScopedAStatus processCaptureRequest(const std::vector<device::CaptureRequest>& requests,
                                      const std::vector<device::BufferCache>& remove,
                                      int32_t* out) override {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& c : remove) {
      auto it = buffers_.find({c.streamId, c.bufferId});
      if (it != buffers_.end()) {
        AHardwareBuffer_release(it->second);
        buffers_.erase(it);
      }
    }
    for (const auto& r : requests) {
      Request request;
      request.frame = r.frameNumber;
      if (!r.settings.metadata.empty()) {
        settings_ = r.settings.metadata;
      }
      request.settings = settings_;
      for (const auto& b : r.outputBuffers) {
        Output output;
        output.stream = b.streamId;
        output.id = b.bufferId;
        auto stream = streams_.find(b.streamId);
        if (stream == streams_.end()) {
          return Err(Status::ILLEGAL_ARGUMENT);
        }
        output.width = stream->second.width;
        output.height = stream->second.height;
        output.blob = stream->second.format == PixelFormat::BLOB;
        output.rgba = stream->second.format == PixelFormat::RGBA_8888;
        output.size = stream->second.bufferSize;
        output.ahb = Import(stream->second, b);
        if (!b.acquireFence.fds.empty()) {
          output.fence = dup(b.acquireFence.fds[0].get());
        }
        request.outputs.push_back(output);
      }
      queue_.push_back(std::move(request));
    }
    *out = requests.size();
    wake_.notify_all();
    return ScopedAStatus::ok();
  }

  ScopedAStatus signalStreamFlush(const std::vector<int32_t>&, int32_t) override {
    return ScopedAStatus::ok();
  }
  ScopedAStatus switchToOffline(const std::vector<int32_t>&, device::CameraOfflineSessionInfo*,
                                std::shared_ptr<device::ICameraOfflineSession>*) override {
    return Err(Status::OPERATION_NOT_SUPPORTED);
  }
  ScopedAStatus repeatingRequestEnd(int32_t, const std::vector<int32_t>&) override {
    return ScopedAStatus::ok();
  }

 private:
  struct Output {
    int stream = 0, width = 0, height = 0, size = 0, fence = -1;
    int64_t id = 0;
    bool blob = false, rgba = false;
    AHardwareBuffer* ahb = nullptr;
  };
  struct Request {
    int frame = 0;
    std::vector<uint8_t> settings;
    std::vector<Output> outputs;
  };

  // Called with mutex_ held. The first request of a buffer carries its handle.
  AHardwareBuffer* Import(const device::Stream& stream, const device::StreamBuffer& b) {
    auto key = std::make_pair(b.streamId, b.bufferId);
    auto it = buffers_.find(key);
    if (it != buffers_.end()) {
      return it->second;
    }
    native_handle_t* handle = native_handle_create(b.buffer.fds.size(), b.buffer.ints.size());
    if (!handle) {
      return nullptr;
    }
    for (size_t i = 0; i < b.buffer.fds.size(); i++) {
      handle->data[i] = dup(b.buffer.fds[i].get());
    }
    std::copy(b.buffer.ints.begin(), b.buffer.ints.end(), handle->data + b.buffer.fds.size());
    bool blob = stream.format == PixelFormat::BLOB;
    AHardwareBuffer_Desc desc{};
    desc.width = blob ? stream.bufferSize : stream.width;
    desc.height = blob ? 1 : stream.height;
    desc.layers = 1;
    desc.format = static_cast<uint32_t>(stream.format);
    desc.usage = static_cast<uint64_t>(stream.usage) | AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;
    desc.stride = desc.width;
    AHardwareBuffer* ahb = nullptr;
    int result = AHardwareBuffer_createFromHandle(&desc, handle, /*AHARDWAREBUFFER_CREATE_FROM_HANDLE_METHOD_CLONE*/ 3, &ahb);
    native_handle_close(handle);
    native_handle_delete(handle);
    if (result != 0) {
      LOGE("buffer import failed: %d", result);
      return nullptr;
    }
    buffers_[key] = ahb;
    return ahb;
  }

  bool Render(const Output& output, const std::vector<uint8_t>& frame, int orientation) {
    if (!output.ahb) {
      return false;
    }
    if (output.fence >= 0) {
      pollfd pfd{output.fence, POLLIN, 0};
      poll(&pfd, 1, 1000);
    }
    if (output.blob || output.rgba) {
      void* data = nullptr;
      if (AHardwareBuffer_lock(output.ahb, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, nullptr,
                               &data) != 0) {
        return false;
      }
      if (output.blob) {
        WriteJpeg(frame, output.width, output.height, orientation, static_cast<uint8_t*>(data),
                  output.size);
      } else {
        AHardwareBuffer_Desc desc;
        AHardwareBuffer_describe(output.ahb, &desc);
        WriteRgb(frame, output.width, output.height, static_cast<uint8_t*>(data), 4,
                 desc.stride * 4);
      }
    } else {
      AHardwareBuffer_Planes planes;
      if (AHardwareBuffer_lockPlanes(output.ahb, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1,
                                     nullptr, &planes) != 0) {
        return false;
      }
      WriteYuv(frame, output.width, output.height, planes);
    }
    AHardwareBuffer_unlock(output.ahb, nullptr);
    return true;
  }

  void Complete(Request& request, bool flushed) {
    auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                   Clock::now().time_since_epoch()).count();
    if (flushed) {
      device::ErrorMsg error;
      error.frameNumber = request.frame;
      error.errorStreamId = -1;
      error.errorCode = device::ErrorCode::ERROR_REQUEST;
      device::NotifyMsg msg;
      msg.set<device::NotifyMsg::error>(error);
      callback_->notify({msg});
    } else {
      device::ShutterMsg shutter;
      shutter.frameNumber = request.frame;
      shutter.timestamp = now;
      shutter.readoutTimestamp = now;
      device::NotifyMsg msg;
      msg.set<device::NotifyMsg::shutter>(shutter);
      callback_->notify({msg});
    }

    device::CaptureResult result;
    result.frameNumber = request.frame;
    result.inputBuffer.streamId = -1;
    result.partialResult = flushed ? 0 : 1;
    bool have_frame = !flushed;
    if (have_frame) {
      if (!webcam_.Get(&frame_)) {
        TestPattern(&frame_, request.frame);
      }
    }
    for (auto& output : request.outputs) {
      device::StreamBuffer buffer;
      buffer.streamId = output.stream;
      buffer.bufferId = output.id;
      bool ok = have_frame && Render(output, frame_, JpegOrientation(request.settings));
      buffer.status = ok ? device::BufferStatus::OK : device::BufferStatus::ERROR;
      if (output.fence >= 0) {
        ::close(output.fence);
      }
      result.outputBuffers.push_back(std::move(buffer));
    }
    if (!flushed) {
      Meta m(request.settings);
      m.I64(ANDROID_SENSOR_TIMESTAMP, {now});
      m.I64(ANDROID_SENSOR_ROLLING_SHUTTER_SKEW, {kFrameNs / 2});
      m.U8(ANDROID_CONTROL_AE_STATE, ANDROID_CONTROL_AE_STATE_CONVERGED);
      m.U8(ANDROID_CONTROL_AF_STATE, ANDROID_CONTROL_AF_STATE_INACTIVE);
      m.U8(ANDROID_CONTROL_AWB_STATE, ANDROID_CONTROL_AWB_STATE_CONVERGED);
      m.U8(ANDROID_FLASH_STATE, ANDROID_FLASH_STATE_UNAVAILABLE);
      m.U8(ANDROID_LENS_STATE, ANDROID_LENS_STATE_STATIONARY);
      m.U8(ANDROID_REQUEST_PIPELINE_DEPTH, 1);
      result.result.metadata = m.Bytes();
    }
    std::vector<device::CaptureResult> results;
    results.push_back(std::move(result));
    callback_->processCaptureResult(results);
  }

  void Worker() {
    auto next = Clock::now();
    std::unique_lock<std::mutex> lock(mutex_);
    while (true) {
      wake_.wait(lock, [this] { return stop_ || !queue_.empty(); });
      if (queue_.empty()) {
        break;
      }
      Request request = std::move(queue_.front());
      queue_.pop_front();
      bool flushed = flushing_ || stop_;
      busy_ = true;
      lock.unlock();
      if (!flushed) {
        next = std::max(next + std::chrono::nanoseconds(kFrameNs),
                        Clock::now() - std::chrono::milliseconds(100));
        std::this_thread::sleep_until(next);
      }
      Complete(request, flushed);
      lock.lock();
      busy_ = false;
      if (queue_.empty()) {
        idle_.notify_all();
      }
    }
  }

  void Stop() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stop_) {
        return;
      }
      stop_ = true;
      wake_.notify_all();
    }
    worker_.join();
    for (auto& [key, ahb] : buffers_) {
      AHardwareBuffer_release(ahb);
    }
    buffers_.clear();
  }

  std::shared_ptr<device::ICameraDeviceCallback> callback_;
  Webcam webcam_;
  std::vector<uint8_t> frame_;
  std::mutex mutex_;
  std::condition_variable wake_, idle_;
  std::deque<Request> queue_;
  std::map<int, device::Stream> streams_;
  std::map<std::pair<int, int64_t>, AHardwareBuffer*> buffers_;
  std::vector<uint8_t> settings_;
  bool stop_ = false, flushing_ = false, busy_ = false;
  std::thread worker_;
};

class Device : public device::BnCameraDevice {
 public:
  ScopedAStatus getCameraCharacteristics(device::CameraMetadata* out) override {
    out->metadata = Characteristics();
    return ScopedAStatus::ok();
  }
  ScopedAStatus getPhysicalCameraCharacteristics(const std::string&,
                                                 device::CameraMetadata*) override {
    return Err(Status::ILLEGAL_ARGUMENT);
  }
  ScopedAStatus getResourceCost(common::CameraResourceCost* out) override {
    out->resourceCost = 100;
    return ScopedAStatus::ok();
  }
  ScopedAStatus isStreamCombinationSupported(const device::StreamConfiguration& config,
                                             bool* out) override {
    *out = true;
    for (const auto& s : config.streams) {
      int format = static_cast<int>(s.format);
      *out = *out &&
             std::find(kSizes.begin(), kSizes.end(), std::make_pair(s.width, s.height)) !=
                 kSizes.end() &&
             (format == kFormatImpl || format == kFormatYuv || format == kFormatBlob);
    }
    return ScopedAStatus::ok();
  }
  ScopedAStatus open(const std::shared_ptr<device::ICameraDeviceCallback>& callback,
                     std::shared_ptr<device::ICameraDeviceSession>* out) override {
    *out = SharedRefBase::make<Session>(callback);
    LOGI("camera opened");
    return ScopedAStatus::ok();
  }
  ScopedAStatus openInjectionSession(const std::shared_ptr<device::ICameraDeviceCallback>&,
                                     std::shared_ptr<device::ICameraInjectionSession>*) override {
    return Err(Status::OPERATION_NOT_SUPPORTED);
  }
  ScopedAStatus setTorchMode(bool) override { return Err(Status::OPERATION_NOT_SUPPORTED); }
  ScopedAStatus turnOnTorchWithStrengthLevel(int32_t) override {
    return Err(Status::OPERATION_NOT_SUPPORTED);
  }
  ScopedAStatus getTorchStrengthLevel(int32_t*) override {
    return Err(Status::OPERATION_NOT_SUPPORTED);
  }
};

class Provider : public provider::BnCameraProvider {
 public:
  ScopedAStatus setCallback(const std::shared_ptr<provider::ICameraProviderCallback>&) override {
    return ScopedAStatus::ok();
  }
  ScopedAStatus getVendorTags(std::vector<common::VendorTagSection>*) override {
    return ScopedAStatus::ok();
  }
  ScopedAStatus getCameraIdList(std::vector<std::string>* out) override {
    out->push_back("device@1.1/internal/0");
    return ScopedAStatus::ok();
  }
  ScopedAStatus getCameraDeviceInterface(const std::string&,
                                         std::shared_ptr<device::ICameraDevice>* out) override {
    *out = device_;
    return ScopedAStatus::ok();
  }
  ScopedAStatus notifyDeviceStateChange(int64_t) override { return ScopedAStatus::ok(); }
  ScopedAStatus getConcurrentCameraIds(
      std::vector<provider::ConcurrentCameraIdCombination>*) override {
    return ScopedAStatus::ok();
  }
  ScopedAStatus isConcurrentStreamCombinationSupported(
      const std::vector<provider::CameraIdAndStreamCombination>&, bool* out) override {
    *out = false;
    return ScopedAStatus::ok();
  }

 private:
  std::shared_ptr<Device> device_ = SharedRefBase::make<Device>();
};

}  // namespace

int main() {
  ABinderProcess_setThreadPoolMaxThreadCount(8);
  ABinderProcess_startThreadPool();
  auto provider_service = SharedRefBase::make<Provider>();
  auto name = std::string(provider::ICameraProvider::descriptor) + "/internal/0";
  binder_status_t status =
      AServiceManager_addService(provider_service->asBinder().get(), name.c_str());
  LOGI("registered: %d", status);
  ABinderProcess_joinThreadPool();
  return 1;
}
