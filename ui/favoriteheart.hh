#ifndef FAVORITEHEART_HH
#define FAVORITEHEART_HH

#include <QObject>
#include <QString>
#include <QRectF>
#include <QColor>

class QPainter;
class QVariantAnimation;

// Shared favorite-heart rendering + "pop" animation. Used by the tracklist and
// queue delegates (which paint into a QPainter at a computed rect) and, later,
// the Now Playing heart button. A single widget can't cover the delegate sites
// (item views have no per-row widgets), so the reusable pieces are a stateless
// painter and a small animation helper.
namespace favheart {

inline constexpr qreal DEFAULT_HEIGHT = 13.0; // glyph height in px

// Centered glyph box (a touch narrower than tall) within a heart column rect.
auto box_in(const QRectF &column, qreal height = DEFAULT_HEIGHT) -> QRectF;

// Paint the heart in `box`, scaled about its center by `scale` (for the pop):
// filled with `on` when favorited, else an outline in `off`. Sets Antialiasing.
auto paint(QPainter *painter, QRectF box, bool filled, const QColor &on,
    const QColor &off, qreal scale = 1.0) -> void;

} // namespace favheart

// Owns the favorite "pop" bump: a single 0→1 sweep mapped to a sin() scale
// (1.0→1.28→1.0). A delegate holds one, calls pop(key) when a heart turns on,
// scales its glyph by scale_for(key) in paint(), and forwards needs_repaint()
// to a viewport update.
class HeartPopAnimator : public QObject {
  Q_OBJECT
public:
  explicit HeartPopAnimator(QObject *parent = nullptr);

  void pop(const QString &key);
  auto scale_for(const QString &key) const -> qreal;

signals:
  void needs_repaint();

private:
  QVariantAnimation *anim;
  QString popping;
  qreal scale = 1.0;
};

#endif /* FAVORITEHEART_HH */
