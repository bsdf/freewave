#include "wavespinner.hh"

#include "anim.hh"
#include "wavepath.hh"

#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

WaveSpinner::WaveSpinner(int w, int h, QWidget *parent)
  : QWidget(parent)
  , sz_w(w)
  , sz_h(h)
{
  setFixedSize(w, h);
  constexpr int tick_ms = 50;
  constexpr double cycle_s = 1.6;
  connect(&timer, &QTimer::timeout, this, [this]() {
    anim::advance_phase(phase, cycle_s, 50);
    update();
  });
  timer.start(tick_ms);
}

void
WaveSpinner::paintEvent(QPaintEvent *)
{
  const int W = width(), H = height();
  const double amp = H * 0.30;
  // Same pen as the FWMark logo wave: the logo strokes 0.16 in a coordinate
  // system scaled by height/3 (i.e. 0.16 * H/3 px). Driving the spinner off its
  // own height the same way makes a spinner and a logo of equal height draw an
  // identical stroke. The small floor only keeps very small spinners visible.
  const double stroke = std::max(1.0, H * 0.16 / 3.0);
  const double cap = stroke / 2.0;

  const double ww = std::max(1.0, double(W) - 2 * cap);
  const int humps = std::max(2, int(std::lround(ww / (3.0 * amp))));
  const double period = 2.0 * (ww / humps);
  const auto path = wave::sine(cap, H / 2.0, ww, period, amp, phase);

  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.setBrush(Qt::NoBrush);
  p.setPen(
      QPen(palette().highlight().color(), stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p.drawPath(path);
}
