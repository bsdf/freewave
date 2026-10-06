#include "fwmark.hh"

#include <QPainter>
#include <QPainterPath>

FWMark::FWMark(int height, QWidget *parent)
  : QWidget(parent)
  , sz_h(height)
{
  setFixedSize(qRound(height * 19.0 / 3.0), height);
}

void
FWMark::set_cross(bool on)
{
  draw_cross = on;
  update();
}

void
FWMark::set_stroke_role(QPalette::ColorRole role)
{
  stroke_role = role;
  update();
}

void
FWMark::paintEvent(QPaintEvent *)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  const double s = double(height()) / 3.0;
  p.translate(0.5 * s, 0.5 * s);
  p.scale(s, s);

  // WAVE path: moveTo(0,0) + 6 cubic segments
  QPainterPath wave;
  wave.moveTo(0, 0);
  wave.cubicTo(1, 0, 2, 2, 3, 2);
  wave.cubicTo(4, 2, 5, 0, 6, 0);
  wave.cubicTo(7, 0, 8, 2, 9, 2);
  wave.cubicTo(10, 2, 11, 0, 12, 0);
  wave.cubicTo(13, 0, 14, 2, 15, 2);
  wave.cubicTo(16, 2, 17, 0, 18, 0);

  // CROSS path: moveTo(3,0) + 1 cubic segment
  QPainterPath cross;
  cross.moveTo(3, 0);
  cross.cubicTo(4, 0, 5, 2, 6, 2);

  const auto stroke_color = palette().color(stroke_role);
  const QPen stroke(stroke_color, 0.16, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin);

  p.setBrush(Qt::NoBrush);

  p.setPen(stroke);
  p.drawPath(wave);

  if (draw_cross)
    {
      p.setPen(stroke);
      p.drawPath(cross);
    }
}
