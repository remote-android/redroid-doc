#include "cuttlefish/host/frontend/redroid_webrtc/mic_injector.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace redroid {
namespace {

using std::chrono::seconds;

TEST(MicGateTest, ClosedUntilFirstSignal) {
  MicGate gate(seconds(5));
  EXPECT_FALSE(gate.Update(false, MicGate::Clock::time_point(seconds(1))));
}

TEST(MicGateTest, HoldsThroughShortSilenceThenCloses) {
  MicGate gate(seconds(5));
  auto start = MicGate::Clock::time_point(seconds(10));
  EXPECT_TRUE(gate.Update(true, start));
  EXPECT_TRUE(gate.Update(false, start + seconds(4)));
  EXPECT_FALSE(gate.Update(false, start + seconds(5)));
  EXPECT_TRUE(gate.Update(true, start + seconds(6)));
}

TEST(HasSignalTest, IgnoresIdleDither) {
  int16_t silent[3] = {0, 0, 0};
  int16_t dither[3] = {1, 0, -1};
  int16_t voice[3] = {0, 0, -1000};
  EXPECT_FALSE(HasSignal(silent, 3));
  EXPECT_FALSE(HasSignal(dither, 3));
  EXPECT_TRUE(HasSignal(voice, 3));
}

}  // namespace
}  // namespace redroid
