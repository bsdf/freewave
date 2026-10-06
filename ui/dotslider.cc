#include "dotslider.hh"

#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace {

// All geometry mirrors WaveSlider so the volume line and the seek bar are drawn
// identically (same stroke widths, caps, and head dot) — only the sine is gone.
constexpr double DOT_R = 2.5; // head-dot radius
constexpr double PAD = DOT_R; // keep the dot off the widget edges
constexpr double CAP = 1.5;   // round-cap radius — keeps the line ends full
constexpr double PEN_UNPLAYED = 1.8;
constexpr double PEN_PLAYED = 2.4;

} // namespace

DotSlider::DotSlider(Qt::Orientation orientation, QWidget *parent)
  : QSlider(orientation, parent)
{
}

QSize
DotSlider::sizeHint() const
{
  return QSize(70, 16);
}

QSize
DotSlider::minimumSizeHint() const
{
  return QSize(40, 16);
}

void
DotSlider::set_from_x(double x)
{
  if (maximum() <= minimum()) return;
  const double track = std::max(1.0, double(width()) - 2 * PAD);
  const double frac = std::clamp((x - PAD) / track, 0.0, 1.0);
  setSliderPosition(minimum() + int(std::lround(frac * (maximum() - minimum()))));
}

void
DotSlider::mousePressEvent(QMouseEvent *e)
{
  if (e->button() == Qt::LeftButton && maximum() > minimum())
    {
      set_from_x(e->position().x());
      e->accept();
      return;
    }
  QSlider::mousePressEvent(e);
}

void
DotSlider::mouseMoveEvent(QMouseEvent *e)
{
  if ((e->buttons() & Qt::LeftButton) && maximum() > minimum())
    {
      set_from_x(e->position().x());
      e->accept();
      return;
    }
  QSlider::mouseMoveEvent(e);
}

void
DotSlider::paintEvent(QPaintEvent *)
{
  const double W = width(), H = height();
  const int range = maximum() - minimum();
  const double frac = range > 0 ? double(value() - minimum()) / range : 0.0;
  // Inset the head by the dot radius so it never clips at the ends, matching
  // WaveSlider; the groove itself still spans the full width.
  const double track = std::max(1.0, W - 2 * PAD);
  const double px = PAD + std::clamp(frac, 0.0, 1.0) * track;
  const double cy = H / 2.0;
  const auto &pal = palette();

  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  // Unplayed line across the full inner span, then the played line over it; the
  // played stroke is drawn last so it wins in the overlap (as WaveSlider does).
  p.setPen(QPen(pal.midlight(), PEN_UNPLAYED, Qt::SolidLine, Qt::RoundCap));
  p.drawLine(QPointF(CAP, cy), QPointF(W - CAP, cy));
  p.setPen(QPen(pal.highlight(), PEN_PLAYED, Qt::SolidLine, Qt::RoundCap));
  p.drawLine(QPointF(CAP, cy), QPointF(px, cy));

  p.setPen(Qt::NoPen);
  p.setBrush(pal.highlight());
  p.drawEllipse(QPointF(px, cy), DOT_R, DOT_R);
}
