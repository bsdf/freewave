#ifndef ALBUMCOLOR_HH
#define ALBUMCOLOR_HH

#include <QColor>
#include <QList>
#include <QPixmap>

namespace albumcolor {

// Below this color_distance(), two colors read as visually "the same" —
// used by extract_palette internally, and by callers that need to dedupe a
// palette against an externally-supplied color (e.g. compute_accent's).
inline constexpr double MIN_COLOR_SEPARATION = 36.0;

// Pick a representative accent color from an album cover.
// Strategy: downscale to 64x64, bucket pixels by hue weighted by
// saturation*value, return the mean color of the winning bucket.
// Returns an invalid QColor if the pixmap is null/empty.
auto compute_accent(const QPixmap &cover) -> QColor;

// Extract up to `count` visually distinct colors from an album cover, most-
// populated first. Strategy: downscale to 64x64, discard the same low-signal
// pixels compute_accent does (achromatic, too dark, too bright, too grey),
// oversplit the remainder via median-cut in RGB space, then greedily keep
// the most-populated boxes whose mean color is at least color_distance()
// MIN_SEPARATION from every color already kept — plain median-cut tends to
// carve a single muted cluster into several near-identical shades, which
// this filters back out. Returns fewer than `count` colors if the cover
// doesn't have that many distinct clusters, or an empty list if the pixmap
// is null/empty.
auto extract_palette(const QPixmap &cover, int count = 5) -> QList<QColor>;

// Euclidean distance between two colors in RGB space; smaller means more
// similar. Used to keep extract_palette results distinct, and to keep a
// palette from duplicating an already-chosen accent color.
auto color_distance(QColor a, QColor b) -> double;

}

#endif
