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

#include "cuttlefish/host/frontend/redroid_webrtc/hwc_frame_source.h"

#include <libyuv.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <deque>

#include "absl/log/log.h"
#include "drm/drm_fourcc.h"

namespace redroid {
namespace {

// An RGBA_8888 (DRM_FORMAT_ABGR8888) frame owning its pixels, tightly packed.
class RgbaFrame : public cuttlefish::VideoFrameBuffer {
 public:
  RgbaFrame(int width, int height)
      : width_(width),
        height_(height),
        data_(std::make_unique_for_overwrite<uint8_t[]>(DataSize())) {}

  int width() const override { return width_; }
  int height() const override { return height_; }
  uint8_t* Data() const override { return data_.get(); }
  int Stride() const override { return width_ * 4; }
  size_t DataSize() const override {
    return static_cast<size_t>(width_) * height_ * 4;
  }
  uint32_t PixelFormat() const override { return DRM_FORMAT_ABGR8888; }
  std::unique_ptr<VideoFrameBuffer> Clone() const override {
    auto clone = std::make_unique<RgbaFrame>(width_, height_);
    memcpy(clone->Data(), Data(), DataSize());
    return clone;
  }

 private:
  int width_;
  int height_;
  std::unique_ptr<uint8_t[]> data_;
};

int32_t ReadInt(const uint8_t* data, size_t index) {
  int32_t value;
  memcpy(&value, data + index * sizeof(int32_t), sizeof(value));
  return value;
}

}  // namespace

std::optional<HwcFrameRecord> ParseHwcFrameRecord(const uint8_t* data,
                                                  size_t size) {
  // native_handle_t: version (= header size in bytes), numFds, numInts, then
  // numFds fd slots and numInts ints. gralloc.redroid's ints start with
  // magic, flags, size, offset.
  if (size < 3 * sizeof(int32_t)) {
    return std::nullopt;
  }
  int version = ReadInt(data, 0);
  int num_fds = ReadInt(data, 1);
  int num_ints = ReadInt(data, 2);
  size_t total = 3 + static_cast<size_t>(num_fds) + num_ints;
  if (version != 12 || num_fds < 1 || num_fds > 4 || num_ints < 4 ||
      total * sizeof(int32_t) > size) {
    return std::nullopt;
  }
  size_t ints = 3 + num_fds;
  if (ReadInt(data, ints) != kGrallocHandleMagic) {
    return std::nullopt;
  }
  HwcFrameRecord record;
  record.num_fds = num_fds;
  record.size = ReadInt(data, ints + 2);
  record.offset = ReadInt(data, ints + 3);
  if (record.size <= 0 || record.offset < 0) {
    return std::nullopt;
  }
  return record;
}

HwcFrameSource::HwcFrameSource(std::string socket_path, int width, int height,
                               FrameCallback on_frame)
    : socket_path_(std::move(socket_path)),
      width_(width),
      height_(height),
      on_frame_(std::move(on_frame)) {}

HwcFrameSource::~HwcFrameSource() {
  stop_ = true;
  int fd = socket_fd_.exchange(-1);
  if (fd >= 0) {
    shutdown(fd, SHUT_RDWR);
  }
  if (thread_.joinable()) {
    thread_.join();
  }
  if (fd >= 0) {
    close(fd);
  }
}

void HwcFrameSource::Start() {
  thread_ = std::thread([this] { Loop(); });
}

void HwcFrameSource::Loop() {
  bool logged_waiting = false;
  while (!stop_) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);
    if (fd >= 0 &&
        connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
      LOG(INFO) << "Connected to hwcomposer stream " << socket_path_;
      logged_waiting = false;
      socket_fd_ = fd;
      connected_ = true;
      StreamFrames(fd);
      connected_ = false;
      if (socket_fd_.exchange(-1) >= 0) {
        close(fd);
      }
      if (!stop_) {
        LOG(WARNING) << "hwcomposer stream closed, reconnecting";
      }
    } else {
      if (fd >= 0) {
        close(fd);
      }
      if (!logged_waiting) {
        LOG(INFO) << "Waiting for the hwcomposer stream at " << socket_path_
                  << " (redroid needs androidboot.use_redroid_stream=1 and "
                     "/ipc shared with this container)";
        logged_waiting = true;
      }
    }
    for (int i = 0; i < 10 && !stop_; i++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
}

bool HwcFrameSource::StreamFrames(int fd) {
  std::vector<uint8_t> pending;
  std::deque<int> fds;
  auto close_fds = [&fds] {
    for (int buffer_fd : fds) {
      close(buffer_fd);
    }
    fds.clear();
  };
  while (!stop_) {
    uint8_t buf[512];
    alignas(cmsghdr) uint8_t control[CMSG_SPACE(16 * sizeof(int))];
    iovec iov{buf, sizeof(buf)};
    msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);
    ssize_t n = TEMP_FAILURE_RETRY(recvmsg(fd, &msg, MSG_CMSG_CLOEXEC));
    if (n <= 0) {
      close_fds();
      return false;
    }
    for (cmsghdr* c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c)) {
      if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) {
        size_t count = (c->cmsg_len - CMSG_LEN(0)) / sizeof(int);
        const int* received = reinterpret_cast<const int*>(CMSG_DATA(c));
        fds.insert(fds.end(), received, received + count);
      }
    }
    pending.insert(pending.end(), buf, buf + n);

    while (pending.size() >= kHwcRecordSize) {
      auto record = ParseHwcFrameRecord(pending.data(), kHwcRecordSize);
      pending.erase(pending.begin(), pending.begin() + kHwcRecordSize);
      if (!record) {
        LOG(ERROR) << "Unexpected hwcomposer stream record, is this a "
                      "redroid image with a different protocol?";
        close_fds();
        return false;
      }
      if (fds.size() < static_cast<size_t>(record->num_fds)) {
        LOG(ERROR) << "hwcomposer frame without buffer fd";
        close_fds();
        return false;
      }
      int buffer_fd = fds.front();
      std::shared_ptr<cuttlefish::VideoFrameBuffer> frame =
          ReadFrame(buffer_fd, *record);
      for (int i = 0; i < record->num_fds; i++) {
        close(fds.front());
        fds.pop_front();
      }
      // The buffer is copied: let hwcomposer send the next frame.
      uint8_t ack = 1;
      if (TEMP_FAILURE_RETRY(send(fd, &ack, 1, MSG_NOSIGNAL)) != 1) {
        close_fds();
        return false;
      }
      if (frame) {
        on_frame_(std::move(frame));
      }
    }
  }
  close_fds();
  return true;
}

std::shared_ptr<cuttlefish::VideoFrameBuffer> HwcFrameSource::ReadFrame(
    int buffer_fd, const HwcFrameRecord& record) {
  size_t stride = static_cast<size_t>((width_ + 1) & ~1) * 4;
  size_t needed = record.offset + stride * (height_ - 1) + width_ * 4;
  if (needed > static_cast<size_t>(record.size)) {
    LOG(ERROR) << "hwcomposer buffer of " << record.size << " bytes is too "
               << "small for " << width_ << "x" << height_
               << ", is --width/--height right?";
    return nullptr;
  }
  void* mapped =
      mmap(nullptr, record.size, PROT_READ, MAP_SHARED, buffer_fd, 0);
  if (mapped == MAP_FAILED) {
    PLOG(ERROR) << "mmap of hwcomposer buffer";
    return nullptr;
  }
  const uint8_t* src = static_cast<const uint8_t*>(mapped) + record.offset;
  int rotation = rotation_;
  bool swap = rotation == 90 || rotation == 270;
  auto frame = std::make_shared<RgbaFrame>(swap ? height_ : width_,
                                           swap ? width_ : height_);
  // Content drawn for a display rotated by R is rotated by R clockwise in
  // the buffer: rotate it back.
  libyuv::RotationMode mode = rotation == 90    ? libyuv::kRotate270
                              : rotation == 180 ? libyuv::kRotate180
                              : rotation == 270 ? libyuv::kRotate90
                                                : libyuv::kRotate0;
  libyuv::ARGBRotate(src, static_cast<int>(stride), frame->Data(),
                     frame->Stride(), width_, height_, mode);
  munmap(mapped, record.size);
  return frame;
}

}  // namespace redroid
