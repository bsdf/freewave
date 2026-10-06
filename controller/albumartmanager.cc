#include "albumartmanager.hh"
#include "controller/backend.hh"
#include "controller/albumcolor.hh"
#include "controller/settings.hh"
#include "ui/textcover.hh"
#include "timeutil.hh"

#include <algorithm>

#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>

constexpr int MAX_CACHE_SZ = 600;

AlbumArtManager::AlbumArtManager(QDir cache_path, Backend *backend,
    int pixmap_cache_limit)
  : cache_path{cache_path}
  , backend{backend}
{
  if (!cache_path.exists())
    {
      spdlog::debug("cache path [{}] doesn't exist, creating...",
          cache_path.path());
      if (!cache_path.mkpath(cache_path.path()))
        spdlog::warn("couldn't make cache path [{}]", cache_path.path());
    }

  QPixmapCache::setCacheLimit(pixmap_cache_limit);

  default_cover = QIcon(":icons/view-media-track-24x24.svg")
                      .pixmap(QSize(200, 200));

  // Build rounded mask for queue thumbnails
  queue_mask = QPixmap(queue_thumb_size);
  queue_mask.fill(Qt::transparent);

  QPainter p(&queue_mask);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(Qt::NoPen);
  p.setBrush(QBrush(Qt::black));
  p.drawEllipse(QRect(QPoint(1, 1), queue_thumb_size - QSize(2, 2)));
  p.end();
}

// ---------------------------------------------------------------------------
// Cache reads — fast, main-thread safe
// ---------------------------------------------------------------------------

auto
AlbumArtManager::get_art(const QString &hash, QSize target) -> QPixmap
{
  QPixmap pixmap;
  auto cache_key = QString("%1-%2x%3").arg(hash).arg(target.width()).arg(target.height());

  if (!QPixmapCache::find(cache_key, &pixmap))
    {
      QString cached = cache_path.filePath(QString("%1.jpg").arg(hash));
      auto img = QImage(cached);
      if (img.isNull())
        {
          auto it = album_meta.constFind(hash);
          if (it != album_meta.constEnd())
            {
              const album &a = it.value();
              QString year = timeutil::year_label(a.date);
              const qreal dpr = qApp->devicePixelRatio();
              const QSize phys(qRound(target.width() * dpr),
                  qRound(target.height() * dpr));
              QColor accent = textcover::deterministic_accent(a.artist + a.name);
              // The full sleeve turns to noise below ~64px (queue thumb,
              // player-bar mini); drop to the monogram tier there. Keyed on the
              // logical target so the breakpoint is dpr-independent.
              const auto style
                  = std::min(target.width(), target.height())
                            <= theme::tok::art::monogram_max
                        ? textcover::style::monogram
                        : textcover::style::sleeve;
              const bool stamp = a.is_live && Settings().bootleg_stamp();
              pixmap = textcover::render(style, a.artist, a.name, year, accent,
                  phys, stamp);
              pixmap.setDevicePixelRatio(dpr);
            }
          else
            {
              pixmap = default_cover.scaled(target, Qt::KeepAspectRatio,
                  Qt::SmoothTransformation);
            }
        }
      else
        pixmap = QPixmap::fromImage(
            img.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation));
      QPixmapCache::insert(cache_key, pixmap);
    }

  return pixmap;
}

auto
AlbumArtManager::get_queue_thumb(const QString &hash) -> QPixmap
{
  QPixmap pixmap;
  auto cache_key = QString("%1-thumb").arg(hash);

  if (!QPixmapCache::find(cache_key, &pixmap))
    {
      auto art = get_art(hash, queue_thumb_size);
      auto scaled = art.scaled(queue_thumb_size,
          Qt::KeepAspectRatioByExpanding,
          Qt::SmoothTransformation);

      QImage pp(queue_thumb_size, QImage::Format_ARGB32_Premultiplied);
      pp.fill(Qt::transparent);

      QPainter p(&pp);
      p.setCompositionMode(QPainter::CompositionMode_SourceOver);
      p.drawPixmap(0, 0, scaled);
      p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
      p.drawPixmap(0, 0, queue_mask);
      p.end();

      pixmap = QPixmap::fromImage(pp);
      QPixmapCache::insert(cache_key, pixmap);
    }

  return pixmap;
}

namespace {

// Aspect-fill one album cover into a mosaic cell (or a placeholder fill if the
// cover isn't available). `pm` is a dpr-aware pixmap; crop in its pixel space.
void
draw_cover_cell(QPainter *p, const QRect &cell, const QPixmap &pm,
    const QColor &placeholder)
{
  if (pm.isNull())
    {
      p->fillRect(cell, placeholder);
      return;
    }
  const qreal dpr = pm.devicePixelRatio();
  auto scaled = pm.scaled(cell.size() * dpr, Qt::KeepAspectRatioByExpanding,
      Qt::SmoothTransformation);
  int sx = (scaled.width() - int(cell.width() * dpr)) / 2;
  int sy = (scaled.height() - int(cell.height() * dpr)) / 2;
  p->drawPixmap(cell, scaled,
      QRect(sx, sy, int(cell.width() * dpr), int(cell.height() * dpr)));
}

} // namespace

void
AlbumArtManager::draw_mosaic(QPainter *p, QRect rect, const QStringList &hashes)
{
  p->setRenderHint(QPainter::Antialiasing, true);
  QPainterPath clip;
  clip.addRoundedRect(rect, 3, 3);
  p->setClipPath(clip);

  const QColor placeholder = QGuiApplication::palette().color(QPalette::Midlight);
  auto cover = [this](const QString &h, QSize sz) {
    return h.isEmpty() ? QPixmap{} : get_art(h, sz);
  };

  if (hashes.isEmpty())
    {
      p->fillRect(rect, placeholder);
      return;
    }
  if (hashes.size() == 1)
    {
      draw_cover_cell(p, rect, cover(hashes.first(), rect.size()), placeholder);
      return;
    }

  int hw = rect.width() / 2;
  int hh = rect.height() / 2;
  QRect cells[4] = {
      QRect(rect.left(), rect.top(), hw, hh),
      QRect(rect.left() + hw, rect.top(), rect.width() - hw, hh),
      QRect(rect.left(), rect.top() + hh, hw, rect.height() - hh),
      QRect(rect.left() + hw, rect.top() + hh, rect.width() - hw, rect.height() - hh),
  };
  for (int i = 0; i < 4; ++i)
    draw_cover_cell(p, cells[i], cover(hashes[i % hashes.size()], cells[i].size()),
        placeholder);
}

auto
AlbumArtManager::mosaic_file(const QString &id, const QString &stamp, QSize size) const
    -> QString
{
  auto tag = QStringLiteral("%1|%2@%3|%4x%5")
                 .arg(cache_scope, id, stamp)
                 .arg(size.width())
                 .arg(size.height());
  auto h = QCryptographicHash::hash(tag.toUtf8(), QCryptographicHash::Md5);
  return cache_path.filePath(QStringLiteral("plm-") + QString::fromLatin1(h.toHex())
                             + QStringLiteral(".png"));
}

bool
AlbumArtManager::has_playlist_mosaic(const QString &id, const QString &stamp,
    QSize size) const
{
  return !cache_scope.isEmpty() && QFileInfo::exists(mosaic_file(id, stamp, size));
}

auto
AlbumArtManager::get_playlist_mosaic(const QString &id, const QString &stamp,
    const QStringList &hashes, QSize size) -> QPixmap
{
  const qreal dpr = qApp->devicePixelRatio();
  const QString key = QStringLiteral("plm|%1|%2@%3|%4x%5")
                          .arg(cache_scope, id, stamp)
                          .arg(size.width())
                          .arg(size.height());
  // Stamp-agnostic key for the last-good hold (see last_mosaic).
  const QString hold_key = QStringLiteral("%1|%2|%3x%4")
                               .arg(cache_scope, id)
                               .arg(size.width())
                               .arg(size.height());

  QPixmap pm;
  if (QPixmapCache::find(key, &pm))
    return pm;

  if (!cache_scope.isEmpty())
    {
      QImage img(mosaic_file(id, stamp, size));
      if (!img.isNull())
        {
          pm = QPixmap::fromImage(img);
          pm.setDevicePixelRatio(dpr);
          QPixmapCache::insert(key, pm);
          last_mosaic.insert(hold_key, pm);
          spdlog::debug("mosaic: disk hit id=[{}] stamp=[{}] {}x{}", id, stamp,
              size.width(), size.height());
          return pm;
        }
    }

  if (hashes.isEmpty())
    {
      // Nothing to build from yet (a reorder cleared the contents mid-refetch).
      // Hold the last fully-rendered mosaic for this playlist rather than
      // flashing a placeholder; it re-renders once the contents land.
      auto held = last_mosaic.constFind(hold_key);
      if (held != last_mosaic.constEnd())
        return held.value();
      // No log here: this runs on every delegate repaint (hover spams it), and an
      // empty playlist never has hashes to resolve.
      return {}; // caller draws a placeholder and fetches the contents
    }

  pm = QPixmap(size * dpr);
  pm.setDevicePixelRatio(dpr);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  draw_mosaic(&p, QRect(QPoint(0, 0), size), hashes);
  p.end();

  // Only cache/persist once every constituent cover is a real JPEG on disk.
  // A fallback album_hash (e.g. a playlist fetched before the library's
  // native_id→hash map loaded) has no matching JPEG, so get_art would draw the
  // default icon — this gate stops that being baked in, and lets the tile
  // re-render as covers land (art_ready repaints) or the hashes get re-resolved.
  bool covers_ready = std::all_of(hashes.cbegin(), hashes.cend(),
      [this](const QString &h) {
        return QFileInfo::exists(cache_path.filePath(h + QStringLiteral(".jpg")));
      });
  spdlog::debug("mosaic: render id=[{}] hashes={} covers_ready={} scope=[{}]", id,
      hashes.size(), covers_ready, cache_scope);
  if (covers_ready)
    {
      QPixmapCache::insert(key, pm);
      last_mosaic.insert(hold_key, pm);
      if (!cache_scope.isEmpty())
        pm.save(mosaic_file(id, stamp, size), "png");
    }
  return pm;
}

auto
AlbumArtManager::get_accent(const QString &hash) -> QColor
{
  auto it = accent_cache.constFind(hash);
  if (it != accent_cache.constEnd())
    return it.value();

  // If no cached JPEG exists and we have album metadata, derive a deterministic
  // accent rather than sampling the painted sleeve (which would be circular).
  QString cached = cache_path.filePath(QString("%1.jpg").arg(hash));
  auto meta_it = album_meta.constFind(hash);
  if (!QFileInfo::exists(cached) && meta_it != album_meta.constEnd())
    {
      const album &a = meta_it.value();
      QColor c = textcover::deterministic_accent(a.artist + a.name);
      accent_cache.insert(hash, c);
      return c;
    }

  // Sample a small pixmap — cheaper than the full-size default and the bucketing
  // algorithm doesn't need detail. Falls back to default cover if no file yet.
  auto px = get_art(hash, theme::tok::art::accent_sample);
  QColor c = albumcolor::compute_accent(px);
  accent_cache.insert(hash, c);
  return c;
}

auto
AlbumArtManager::get_palette(const QString &hash, int count) -> QList<QColor>
{
  if (count <= 0)
    return {};

  QString cache_key = QString("%1:%2").arg(hash).arg(count);
  auto it = palette_cache.constFind(cache_key);
  if (it != palette_cache.constEnd())
    return it.value();

  // The palette's first color is always get_accent's — the one already used
  // for chips, badges, and the now-playing wash — so a caller asking for
  // "the accent plus a few more" gets one coherent set instead of two
  // algorithms disagreeing about the primary color.
  QColor accent = get_accent(hash);
  QList<QColor> palette;
  if (accent.isValid())
    palette.push_back(accent);

  // Same rationale as get_accent: a text-jacket cover has no real art to
  // sample, so derive the rest of the palette deterministically instead of
  // sampling the rendered sleeve.
  QString cached = cache_path.filePath(QString("%1.jpg").arg(hash));
  auto meta_it = album_meta.constFind(hash);
  QList<QColor> others;
  if (!QFileInfo::exists(cached) && meta_it != album_meta.constEnd())
    others = textcover::deterministic_palette(meta_it.value().artist + meta_it.value().name, count);
  else
    others = albumcolor::extract_palette(get_art(hash, theme::tok::art::accent_sample), count);

  for (QColor c : others)
    {
      if (palette.size() >= count)
        break;
      bool too_close = std::any_of(palette.begin(), palette.end(), [&](QColor kept) {
        return albumcolor::color_distance(kept, c) < albumcolor::MIN_COLOR_SEPARATION;
      });
      if (!too_close)
        palette.push_back(c);
    }

  palette_cache.insert(cache_key, palette);
  return palette;
}

// ---------------------------------------------------------------------------
// Thumbnail rebuild — async pipeline driven by album_art_received signal
// ---------------------------------------------------------------------------

auto
AlbumArtManager::rebuild_thumbnails(const QList<album> &albums) -> void
{
  // Drop any in-flight request so its response can't land on the wrong album
  disconnect(art_conn);
  art_conn = {};

  emit thumbnail_updating(true);
  QPixmapCache::clear();
  accent_cache.clear();
  palette_cache.clear();
  album_meta.clear();

  rebuilding = false;
  art_queue.clear();
  art_fetch_failures.clear();

  for (const auto &a : albums)
    album_meta[a.album_hash] = a;

  if (!backend)
    {
      emit thumbnail_updating(false);
      return;
    }

  for (const auto &a : albums)
    {
      QString cached = cache_path.filePath(QString("%1.jpg").arg(a.album_hash));
      if (cache_path.exists(cached) || has_noart_marker(a))
        continue;
      art_queue.append(a);
    }

  // Newest additions first: they're the likeliest to lack cached art, and the
  // user is likeliest to be looking at them.
  std::stable_sort(art_queue.begin(), art_queue.end(),
      [](const album &a, const album &b) {
        return a.last_modified > b.last_modified;
      });

  rebuilding = true;
  fetch_next();
}

auto
AlbumArtManager::fetch_next() -> void
{
  if (art_queue.isEmpty())
    {
      rebuilding = false;
      emit thumbnail_updating(false);
      return;
    }

  current_art_album = art_queue.takeFirst();
  auto expected_hash = current_art_album.album_hash;
  auto expected_uri = current_art_album.uri;
  // Connect for exactly this request: filter by URI so a stale response from a
  // previous (now-disconnected) request can't land on the wrong album's file.
  art_conn = connect(backend, &Backend::album_art_received, this,
      [this, expected_hash, expected_uri](QString uri, QByteArray bytes) {
        if (uri != expected_uri) return;
        QObject::disconnect(art_conn);
        art_conn = {};
        process_art(expected_hash, std::move(bytes));
      });
  backend->fetch_album_art(current_art_album.uri);
}

auto
AlbumArtManager::process_art(QString expected_hash,
    QByteArray bytes) -> void
{
  if (!rebuilding) return;
  // Guard against any edge case where the hash slipped through mismatched
  if (current_art_album.album_hash != expected_hash)
    {
      fetch_next();
      return;
    }

  if (!bytes.isEmpty())
    {
      QPixmap px;
      if (px.loadFromData(bytes))
        {
          QString cached = cache_path.filePath(
              QString("%1.jpg").arg(expected_hash));
          if (px.width() > MAX_CACHE_SZ || px.height() > MAX_CACHE_SZ)
            px = px.scaled(QSize(MAX_CACHE_SZ, MAX_CACHE_SZ),
                Qt::KeepAspectRatio, Qt::SmoothTransformation);
          px.save(cached, "jpeg", 80);
          QFile::remove(noart_file(expected_hash));
          spdlog::debug("AlbumArtManager: cached art for [{}]", expected_hash);
          emit art_ready(expected_hash);
        }
      else
        {
          art_fetch_failures.append(current_art_album);
          write_noart_marker(current_art_album);
          spdlog::trace("AlbumArtManager: loadFromData failed for [{} - {} ({})]",
              current_art_album.artist, current_art_album.name, expected_hash);
        }
    }
  else
    {
      art_fetch_failures.append(current_art_album);
      write_noart_marker(current_art_album);
      spdlog::trace("AlbumArtManager: no art for [{} - {} ({})]",
          current_art_album.artist, current_art_album.name, expected_hash);
    }

  fetch_next();
}

// ---------------------------------------------------------------------------
// Negative art cache
// ---------------------------------------------------------------------------

auto
AlbumArtManager::noart_file(const QString &hash) const -> QString
{
  return cache_path.filePath(hash + QStringLiteral(".noart"));
}

auto
AlbumArtManager::has_noart_marker(const album &a) const -> bool
{
  QFile f{noart_file(a.album_hash)};
  if (!f.open(QIODevice::ReadOnly))
    return false;
  return QString::fromUtf8(f.readAll()) == a.uri;
}

auto
AlbumArtManager::write_noart_marker(const album &a) -> void
{
  QFile f{noart_file(a.album_hash)};
  if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    f.write(a.uri.toUtf8());
}

auto
AlbumArtManager::clear_noart_markers() -> void
{
  auto markers = cache_path.entryList(QStringList() << "*.noart");
  for (const auto &m : markers)
    cache_path.remove(m);
  spdlog::debug("AlbumArtManager: cleared {} no-art markers", markers.size());
}

// ---------------------------------------------------------------------------
// Thumbnail cleanup
// ---------------------------------------------------------------------------

auto
AlbumArtManager::clean_thumbnails(const QList<album> &albums) -> void
{
  cache_path.mkdir("tmp");

  auto thumbs = cache_path.entryList(QStringList() << "*.jpg" << "*.noart");
  for (const auto &thumb : thumbs)
    cache_path.rename(thumb, QString("tmp/%1").arg(thumb));

  for (const auto &a : albums)
    for (const auto *ext : {".jpg", ".noart"})
      {
        auto filename = a.album_hash + QString::fromLatin1(ext);
        cache_path.rename(QString("tmp/%1").arg(filename), filename);
      }

  auto tmp_path = QDir{cache_path};
  tmp_path.cd("tmp");
  tmp_path.removeRecursively();
}
