#include "covermosaic.hh"

#include <QColor>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRect>

#include "ui/favoriteheart.hh"

namespace covermosaic {

void
paint_heart(QPainter *p, const QRect &rect, const QColor &accent,
    const QColor &glyph)
{
  p->save();
  p->setRenderHint(QPainter::Antialiasing, true);

  QLinearGradient g(rect.topLeft(), rect.bottomRight());
  g.setColorAt(0.0, accent);
  auto darker = accent.darker(130);
  g.setColorAt(1.0, darker);

  QPainterPath tile;
  tile.addRoundedRect(rect, 3, 3);
  p->fillPath(tile, g);

  auto box = favheart::box_in(QRectF(rect), rect.height() * 0.42);
  favheart::paint(p, box, true, glyph, glyph);

  p->restore();
}

} // namespace covermosaic
