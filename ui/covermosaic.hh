#ifndef COVERMOSAIC_HH
#define COVERMOSAIC_HH

class QPainter;
class QRect;
class QColor;

// Paint helper for the pinned "Favorited Tracks" tile. Colors come from the
// caller (the delegate's option.palette / accent) so it stays palette-agnostic.
// The album-cover mosaic itself lives in AlbumArtManager::get_playlist_mosaic.
namespace covermosaic {

// The pinned "Favorited Tracks" tile: an accent gradient with a centered heart.
void paint_heart(QPainter *p, const QRect &rect, const QColor &accent,
    const QColor &glyph);

} // namespace covermosaic

#endif // COVERMOSAIC_HH
