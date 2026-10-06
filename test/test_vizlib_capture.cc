#include "vizlib/pipewirecapture.hh"

#include <gtest/gtest.h>

// Construction must not touch PipeWire (pw_init happens in start()), so these
// run anywhere — no daemon required. Actually starting a capture needs a live
// PipeWire session, so that path is left to manual / integration verification.

TEST(PipeWireCapture, ConstructsInactive)
{
  viz::PipeWireCapture cap;
  EXPECT_FALSE(cap.running());
}

TEST(PipeWireCapture, DrainsEmptyBeforeStart)
{
  viz::PipeWireCapture cap;
  float dst[16];
  EXPECT_EQ(cap.read(dst, 16), 0u);
  EXPECT_EQ(cap.available(), 0u);
}

TEST(PipeWireCapture, FixedCaptureRate)
{
  // The analyzer is constructed against this rate, so it must stay a known
  // constant (PipeWire converts the sink monitor to it for us).
  EXPECT_EQ(viz::PipeWireCapture::RATE, 48000u);
}
