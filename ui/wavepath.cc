#include "wavepath.hh"

#include "anim.hh"

#include <algorithm>
#include <cmath>

namespace wave {

QPainterPath
sine(double x0, double cy, double width, double period, double amp, double phase)
{
  QPainterPath p;
  if (width <= 0.0 || period <= 0.0)
    {
      p.moveTo(x0, cy);
      return p;
    }

  const double w = anim::tau / period;
  // ~1px sampling — visually indistinguishable from a continuous sine.
  const int steps = std::max(2, int(std::lround(width)));
  for (int i = 0; i <= steps; ++i)
    {
      const double x = x0 + width * (double(i) / steps);
      const double y = cy - amp * std::sin(w * (x - x0) + phase);
      if (i == 0)
        p.moveTo(x, y);
      else
        p.lineTo(x, y);
    }
  return p;
}

} // namespace wave
