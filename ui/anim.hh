#ifndef ANIM_HH
#define ANIM_HH

#include <numbers>

namespace anim {

inline constexpr double tau = 2.0 * std::numbers::pi;

// Advance a running animation phase (radians) by one timer tick and wrap it back
// into [0, tau). `cycle_s` is the seconds per full 2*pi cycle; `tick_ms` is the
// timer interval — so the on-screen speed stays fixed regardless of the tick rate.
inline void
advance_phase(double &phase, double cycle_s, int tick_ms)
{
  phase += tau * (double(tick_ms) / 1000.0) / cycle_s;
  if (phase >= tau) phase -= tau;
}

} // namespace anim

#endif // ANIM_HH
