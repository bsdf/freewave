#ifndef TEXTCOVER_HH
#define TEXTCOVER_HH

#include <QColor>
#include <QList>
#include <QPixmap>
#include <QSize>
#include <QString>

namespace textcover {

enum class style { sleeve,
  monogram };

auto deterministic_accent(const QString &key) -> QColor;

// A palette of `count` colors hue-spread from deterministic_accent(key), for
// albums that have no real cover to sample (text-jacket covers) — used
// wherever a multi-color palette is needed and get_accent's single-color
// fallback isn't enough.
auto deterministic_palette(const QString &key, int count) -> QList<QColor>;

auto render(style s, const QString &artist, const QString &title,
    const QString &year, const QColor &accent, QSize size,
    bool stamp = true, int radius = 4) -> QPixmap;

} // namespace textcover

#endif // TEXTCOVER_HH
