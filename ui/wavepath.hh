#ifndef WAVEPATH_HH
#define WAVEPATH_HH

#include <QPainterPath>

namespace wave {

// Canonical sine path shared by every wave the UI draws — the transport
// scrubber, the onboarding progress bar and the loading spinner. The curve is a
// true sine sampled into a fine polyline (it's just a line): y oscillates by
// ±`amp` about the centreline `cy` over [x0, x0 + width], with one full period
// every `period` and an optional `phase` (radians) for animation.
//
// Pick `period` so that width / period is a whole number to land back on the
// centreline at both ends when phase == 0 (the round-cap-friendly case). For an
// animated wiggle, hold the geometry fixed and advance `phase` each frame.
//
// The app-wide convention is period == 6 * amp (peak-to-peak == period / 3) —
// the same ratio as the FWMark logo wave.
QPainterPath sine(double x0, double cy, double width, double period, double amp,
    double phase = 0.0);

} // namespace wave

#endif // WAVEPATH_HH
