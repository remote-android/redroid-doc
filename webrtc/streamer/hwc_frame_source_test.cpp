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

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

namespace redroid {
namespace {

// A record sent by redroid 14's hwcomposer for a 720x1280 display.
std::vector<uint8_t> RealRecord() {
  const int32_t ints[] = {12,      1,         8,         18,    0x3141592, 0,
                          3690496, 0,         269443072, 58826, 94,        0,
                          0x30103, 0x9110000, 0,         0};
  std::vector<uint8_t> record(kHwcRecordSize, 0xff);
  memcpy(record.data(), ints, sizeof(ints));
  return record;
}

TEST(HwcFrameSourceTest, ParsesGrallocHandle) {
  auto record = RealRecord();
  auto parsed = ParseHwcFrameRecord(record.data(), record.size());
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->num_fds, 1);
  EXPECT_EQ(parsed->size, 3690496);
  EXPECT_EQ(parsed->offset, 0);
}

TEST(HwcFrameSourceTest, RejectsOtherData) {
  auto record = RealRecord();
  record[16] ^= 0xff;  // magic
  EXPECT_FALSE(ParseHwcFrameRecord(record.data(), record.size()).has_value());
  record = RealRecord();
  record[0] = 16;  // native_handle version
  EXPECT_FALSE(ParseHwcFrameRecord(record.data(), record.size()).has_value());
  EXPECT_FALSE(ParseHwcFrameRecord(record.data(), 8).has_value());
}

}  // namespace
}  // namespace redroid
