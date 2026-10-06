#include "toggle.hh"

#include <QPainter>
#include <QPropertyAnimation>

Toggle::Toggle(QWidget *parent)
  : QWidget(parent)
  , anim(new QPropertyAnimation(this, "thumb_x", this))
{
  setFixedSize(36, 20);
  anim->setDuration(150);
  tx = 2;
}

auto
Toggle::setChecked(bool v) -> void
{
  if (v == checked)
    return;
  checked = v;
  int target = v ? 18 : 2;
  anim->stop();
  anim->setStartValue(tx);
  anim->setEndValue(target);
  anim->start();
  emit toggled(v);
}

void
Toggle::paintEvent(QPaintEvent *)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  auto pal = palette();
  QColor track = checked ? pal.color(QPalette::Highlight) : pal.color(QPalette::Midlight);
  p.setBrush(track);
  p.setPen(Qt::NoPen);
  p.drawRoundedRect(rect(), 10, 10);

  if (!checked)
    {
      p.setBrush(Qt::NoBrush);
      p.setPen(QPen(pal.color(QPalette::Midlight), 1));
      p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
    }

  p.setBrush(Qt::white);
  p.setPen(Qt::NoPen);
  p.drawEllipse(QRect(tx, 2, 16, 16));
}

void
Toggle::mousePressEvent(QMouseEvent *)
{
  setChecked(!checked);
}
