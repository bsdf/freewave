#ifndef ALBUMARTMANAGER_HH
#define ALBUMARTMANAGER_HH

#include <vector>
#include <cstdint>

#include <QDir>
#include <QPixmap>
#include <QPixmapCache>
#include <QFileInfo>
#include <QHash>
#include <QColor>
#include <QByteArray>
#include <QStringList>

#include "model/album.hh"
#include "ui/theme/tokens.hh"

class Backend;
class QPainter;

class AlbumArtManager : public QObject {
  Q_OBJECT
public:
  // backend is used to fetch art via fetch_album_art / album_art_received.
  // Pass nullptr (or omit) to construct without a live backend connection —
  // cache reads still work, but rebuild_thumbnails will be a no-op.
  explicit AlbumArtManager(QDir cache_path,
      Backend *backend = nullptr,
      int pixmap_cache_limit = 10240);

  QPixmap get_art(const QString &hash, QSize target = theme::tok::art::grid);
  QPixmap get_queue_thumb(const QString &hash);

  // A playlist's cover mosaic (2x2 of its first album covers), composited from
  // the album-art cache and persisted to disk keyed by the playlist id + a
  // version stamp (namespaced per active server via set_cache_scope) — so a warm
  // start draws it without re-fetching the playlist's contents. `hashes` supplies
  // the album hashes on a cache miss; empty + nothing cached returns a null
  // pixmap (the caller draws a placeholder and fetches the contents).
  QPixmap get_playlist_mosaic(const QString &id, const QString &stamp,
      const QStringList &hashes, QSize size);
  // Whether a persisted mosaic exists at `size`, so callers can skip fetching
  // the playlist's contents.
  bool has_playlist_mosaic(const QString &id, const QString &stamp, QSize size) const;
  // Namespace the persisted mosaic cache to the active server profile.
  void set_cache_scope(const QString &scope)
  {
    if (cache_scope != scope)
      last_mosaic.clear(); // another profile's held tiles must not leak across
    cache_scope = scope;
  }

  // Representative accent color derived from the album cover.
  // Returns an invalid QColor if cover is unavailable or has no chromatic dominant.
  QColor get_accent(const QString &hash);

  // A palette of visually distinct colors drawn from the album cover, for
  // visualizations that want more than one color. The first entry is always
  // get_accent's color; the rest fill in by extracted population, skipping
  // anything too close (see albumcolor::MIN_COLOR_SEPARATION) to a color
  // already kept. Returns fewer than `count` colors if the cover doesn't
  // have that many distinct clusters; empty if the cover is unavailable.
  QList<QColor> get_palette(const QString &hash, int count = 5);

  // Albums for which art could not be fetched during the last rebuild.
  // Cleared at the start of each rebuild_thumbnails call.
  auto get_art_fetch_failures() const -> QList<album> { return art_fetch_failures; }

  // Drop rendered pixmaps without re-fetching art — used when a render-affecting
  // preference (e.g. the bootleg stamp) changes and covers must repaint.
  auto clear_render_cache() -> void { QPixmapCache::clear(); }

public slots:
  void rebuild_thumbnails(const QList<album> &albums);
  void clean_thumbnails(const QList<album> &albums);
  // Forget all "no art" markers so the next rebuild refetches artless albums.
  // The recovery path for servers whose fetch uri doesn't change when art is
  // added (MPD: the uri is a song path) — wired to the manual Refresh actions.
  void clear_noart_markers();

signals:
  void thumbnail_updating(bool updating);
  // Emitted when a single album's cover JPEG has just been written to disk.
  void art_ready(QString album_hash);

private:
  void fetch_next();
  void process_art(QString expected_hash, QByteArray bytes);
  // Negative art cache: a `<hash>.noart` file records "server has no usable
  // art for this album". It stores the fetch uri, so a changed coverArt id
  // (e.g. a server-side art upload) invalidates the marker naturally.
  auto noart_file(const QString &hash) const -> QString;
  auto has_noart_marker(const album &a) const -> bool;
  auto write_noart_marker(const album &a) -> void;
  void draw_mosaic(QPainter *p, QRect rect, const QStringList &hashes);
  auto mosaic_file(const QString &id, const QString &stamp, QSize size) const -> QString;

  QDir cache_path;
  QString cache_scope; // active profile id — namespaces the persisted mosaics
  Backend *backend = nullptr;

  QPixmap default_cover;
  QPixmap queue_mask;
  QSize queue_thumb_size = theme::tok::art::thumb;

  // Async pipeline state — guarded by 'rebuilding'
  bool rebuilding = false;
  QList<album> art_queue;
  album current_art_album;
  QMetaObject::Connection art_conn; // single active per-request connection

  QList<album> art_fetch_failures;
  QHash<QString, QColor> accent_cache;
  QHash<QString, QList<QColor>> palette_cache;
  QHash<QString, album> album_meta;
  // Last fully-rendered mosaic per (scope,id,size), stamp-agnostic. A reorder
  // bumps the stamp and clears the song cache, so for one frame the delegate
  // asks for a mosaic that can't be built yet (new stamp uncached + no hashes) —
  // serve the previous one instead of a placeholder so the tile doesn't flash.
  QHash<QString, QPixmap> last_mosaic;
};

#endif // ALBUMARTMANAGER_HH
