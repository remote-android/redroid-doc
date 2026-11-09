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

#include "cuttlefish/host/frontend/redroid_webrtc/h264_passthrough.h"

#include <gtest/gtest.h>

#include <vector>

namespace redroid {
namespace {

// SPS + PPS of a scrcpy stream from redroid 14 (constrained baseline, level
// 4.1, 720x1280), as sent in the config packet.
const std::vector<uint8_t> kConfig = {
    0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0xc0, 0x29, 0x8d, 0x68, 0x0b,
    0x40, 0xa1, 0xa4, 0x20, 0x20, 0x20, 0x20, 0xf0, 0x88, 0x46, 0xa0,
    0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x01, 0xa8, 0x35, 0xc8,
};
const std::vector<uint8_t> kIdr = {0x00, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84};
const std::vector<uint8_t> kSlice = {0x00, 0x00, 0x00, 0x01, 0x41, 0x9a, 0x02};

TEST(H264PassthroughTest, PictureSizeFromSps) {
  auto size = H264PictureSize(kConfig.data(), kConfig.size());
  ASSERT_TRUE(size.has_value());
  EXPECT_EQ(size->first, 720);
  EXPECT_EQ(size->second, 1280);
  EXPECT_FALSE(H264PictureSize(kIdr.data(), kIdr.size()).has_value());
}

TEST(H264PassthroughTest, AssemblerPrependsConfigToKeyFrames) {
  std::vector<std::shared_ptr<EncodedH264Frame>> frames;
  H264FrameAssembler assembler(
      [&frames](std::shared_ptr<EncodedH264Frame> f) { frames.push_back(f); });

  // Media before any SPS cannot be decoded and is dropped.
  assembler.OnPacket(kSlice.data(), kSlice.size(), false, false);
  EXPECT_TRUE(frames.empty());

  assembler.OnPacket(kConfig.data(), kConfig.size(), true, false);
  EXPECT_EQ(assembler.width(), 720);
  EXPECT_EQ(assembler.height(), 1280);
  assembler.OnPacket(kIdr.data(), kIdr.size(), false, true);
  assembler.OnPacket(kSlice.data(), kSlice.size(), false, false);
  assembler.OnPacket(kIdr.data(), kIdr.size(), false, true);

  ASSERT_EQ(frames.size(), 3u);
  std::vector<uint8_t> key(kConfig);
  key.insert(key.end(), kIdr.begin(), kIdr.end());
  for (int i : {0, 2}) {
    EXPECT_TRUE(frames[i]->key_frame());
    EXPECT_EQ(std::vector<uint8_t>(frames[i]->Data(),
                                   frames[i]->Data() + frames[i]->DataSize()),
              key);
  }
  EXPECT_FALSE(frames[1]->key_frame());
  EXPECT_EQ(std::vector<uint8_t>(frames[1]->Data(),
                                 frames[1]->Data() + frames[1]->DataSize()),
            kSlice);
  EXPECT_EQ(frames[0]->sequence() + 1, frames[1]->sequence());
  EXPECT_EQ(frames[1]->sequence() + 1, frames[2]->sequence());
  EXPECT_EQ(frames[1]->PixelFormat(), kEncodedH264Format);
  EXPECT_EQ(frames[1]->width(), 720);
}

}  // namespace
}  // namespace redroid
