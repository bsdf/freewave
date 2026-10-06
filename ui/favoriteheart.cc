#include "favoriteheart.hh"

#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>

#include <cmath>
#include <numbers>

namespace {

constexpr qreal ASPECT = 0.86; // width/height — narrower than tall

// Heart outline in the unit box [0,1]², tip at the bottom-center, mapped onto box.
auto
heart_path(const QRectF &box) -> QPainterPath
{
  auto px = [&](qreal ux) { return box.left() + ux * box.width(); };
  auto py = [&](qreal uy) { return box.top() + uy * box.height(); };

  QPainterPath p;
  p.moveTo(px(0.5), py(0.30));
  p.cubicTo(px(0.5), py(0.10), px(0.0), py(0.05), px(0.0), py(0.35));
  p.cubicTo(px(0.0), py(0.58), px(0.30), py(0.74), px(0.5), py(0.95));
  p.cubicTo(px(0.70), py(0.74), px(1.0), py(0.58), px(1.0), py(0.35));
  p.cubicTo(px(1.0), py(0.05), px(0.5), py(0.10), px(0.5), py(0.30));
  p.closeSubpath();
  return p;
}

} // namespace

auto
favheart::box_in(const QRectF &column, qreal height) -> QRectF
{
  QRectF box(0, 0, height * ASPECT, height);
  box.moveCenter(column.center());
  return box;
}

auto
favheart::paint(QPainter *painter, QRectF box, bool filled, const QColor &on,
    const QColor &off, qreal scale) -> void
{
  if (scale != 1.0)
    {
      auto c = box.center();
      box.setSize(box.size() * scale);
      box.moveCenter(c);
    }

  auto path = heart_path(box);
  painter->setRenderHint(QPainter::Antialiasing, true);
  if (filled)
    {
      painter->setPen(Qt::NoPen);
      painter->setBrush(on);
    }
  else
    {
      painter->setBrush(Qt::NoBrush);
      painter->setPen(QPen(off, 1.3));
    }
  painter->drawPath(path);
}

HeartPopAnimator::HeartPopAnimator(QObject *parent)
  : QObject{parent}
{
  // Single 0→1 sweep mapped to a sin() bump (1.0→1.28→1.0).
  anim = new QVariantAnimation(this);
  anim->setStartValue(0.0);
  anim->setEndValue(1.0);
  anim->setDuration(280);
  anim->setEasingCurve(QEasingCurve::OutQuad);
  connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
    scale = 1.0 + 0.28 * std::sin(v.toReal() * std::numbers::pi_v<qreal>);
    emit needs_repaint();
  });
  connect(anim, &QVariantAnimation::finished, this, [this] {
    scale = 1.0;
    popping.clear();
    emit needs_repaint();
  });
}

auto
HeartPopAnimator::pop(const QString &key) -> void
{
  popping = key;
  anim->stop();
  anim->start();
}

auto
HeartPopAnimator::scale_for(const QString &key) const -> qreal
{
  return key == popping ? scale : 1.0;
}
