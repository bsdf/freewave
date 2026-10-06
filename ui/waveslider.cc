#include "waveslider.hh"

#include "wavepath.hh"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace {

constexpr double DOT_R = 2.5; // head-dot radius
constexpr double PAD = DOT_R; // keep the play position off the widget edges

} // namespace

WaveSlider::WaveSlider(Qt::Orientation orientation, QWidget *parent)
  : QSlider(orientation, parent)
{
}

QSize
WaveSlider::sizeHint() const
{
  return QSize(160, 20);
}

QSize
WaveSlider::minimumSizeHint() const
{
  return QSize(40, 20);
}

void
WaveSlider::seek_to_x(double x)
{
  if (maximum() <= minimum()) return;
  const double track = std::max(1.0, double(width()) - 2 * PAD);
  const double frac = std::clamp((x - PAD) / track, 0.0, 1.0);
  setSliderPosition(minimum() + int(std::lround(frac * (maximum() - minimum()))));
}

void
WaveSlider::mousePressEvent(QMouseEvent *e)
{
  if (e->button() == Qt::LeftButton && maximum() > minimum())
    {
      setFocus(Qt::MouseFocusReason); // arm arrow-key seeking after a click
      setSliderDown(true);            // emits sliderPressed() (no handle to drag)
      seek_to_x(e->position().x());
      e->accept();
      return;
    }
  QSlider::mousePressEvent(e);
}

void
WaveSlider::mouseMoveEvent(QMouseEvent *e)
{
  if ((e->buttons() & Qt::LeftButton) && maximum() > minimum())
    {
      seek_to_x(e->position().x());
      e->accept();
      return;
    }
  QSlider::mouseMoveEvent(e);
}

void
WaveSlider::mouseReleaseEvent(QMouseEvent *e)
{
  if (isSliderDown())
    {
      setSliderDown(false); // emits sliderReleased()
      e->accept();
      return;
    }
  QSlider::mouseReleaseEvent(e);
}

void
WaveSlider::paintEvent(QPaintEvent *)
{
  const int W = width(), H = height();
  const int range = maximum() - minimum();
  const double frac = range > 0 ? double(value() - minimum()) / range : 0.0;
  // Inset the play position by the dot radius so the head never clips at the
  // ends; the wave itself still spans the full width.
  const double track = std::max(1.0, double(W) - 2 * PAD);
  const double px = PAD + std::clamp(frac, 0.0, 1.0) * track;
  const auto &pal = palette();

  // Fit a whole number of humps so the wave lands on the centreline (zero
  // amplitude) at both ends, and inset by the round-cap radius (CAP) so the caps
  // render in full instead of being sliced off by the widget edge. Wavelength
  // targets ~1.5*H (the onboarding waves' feel); round to the nearest integer
  // hump count and stretch the period to divide the inner span exactly.
  constexpr double CAP = 1.5; // round-cap radius (>= half the thickest stroke)
  const double ww = std::max(1.0, double(W) - 2 * CAP);
  const double amp = H * 0.375; // true peak height
  const int humps = std::max(1, int(std::lround(ww / (1.5 * H))));
  const double period = 2.0 * (ww / humps);
  const auto path = wave::sine(CAP, H / 2.0, ww, period, amp);

  // Split the wave into played (highlight) and unplayed (dim) at exactly `px`.
  // A hard setClipRect() would snap the seam to whole pixels and make the fill
  // "step" on slow (long) tracks, so instead each stroke uses a gradient pen
  // whose colour fades to transparent across a ~1px band at the play position —
  // an anti-aliased, sub-pixel boundary. The played stroke is drawn last so it
  // wins in the overlap band.
  const double seam = W > 0 ? std::clamp(px / W, 0.0, 1.0) : 0.0;
  const double band = W > 0 ? std::clamp(0.75 / W, 0.0, 0.49) : 0.0;

  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  {
    QColor dim = pal.midlight().color(), dim0 = dim;
    dim0.setAlpha(0);
    QLinearGradient g(0, 0, W, 0);
    g.setColorAt(std::clamp(seam - band, 0.0, 1.0), dim0);
    g.setColorAt(std::clamp(seam + band, 0.0, 1.0), dim);
    p.setPen(QPen(QBrush(g), 1.8, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
  }

  {
    QColor hi = pal.highlight().color(), hi0 = hi;
    hi0.setAlpha(0);
    QLinearGradient g(0, 0, W, 0);
    g.setColorAt(std::clamp(seam - band, 0.0, 1.0), hi);
    g.setColorAt(std::clamp(seam + band, 0.0, 1.0), hi0);
    p.setPen(QPen(QBrush(g), 2.4, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
  }

  // Head dot sits exactly on the curve at x == px. The path's x is monotonic,
  // so find the first sample at/after px and linearly interpolate its y with
  // the previous one for a sub-pixel-accurate position.
  QPointF head(px, H / 2.0);
  const int samples = 256;
  QPointF prev = path.pointAtPercent(0.0);
  for (int i = 1; i <= samples; ++i)
    {
      const QPointF q = path.pointAtPercent(double(i) / samples);
      if (q.x() >= px || i == samples)
        {
          const double span = q.x() - prev.x();
          const double t = span > 1e-6 ? std::clamp((px - prev.x()) / span, 0.0, 1.0) : 0.0;
          head = QPointF(px, prev.y() + t * (q.y() - prev.y()));
          break;
        }
      prev = q;
    }
  p.setPen(Qt::NoPen);
  p.setBrush(pal.highlight().color());
  p.drawEllipse(head, DOT_R, DOT_R);
}
