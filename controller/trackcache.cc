#include "trackcache.hh"

#include <algorithm>
#include <utility>

#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>

// Bumped whenever the bytes behind a track id could differ from what an older
// build stored. Every stream URL the backend builds asks for format=raw — the
// server's own file — so the profile scope and the track id are the whole
// identity today. A transcoding option would have to add its parameters here,
// or a cached transcode would be served for a request that asked for something
// else.
static constexpr auto KEY_VERSION = "v1:raw";

static constexpr auto INDEX_FILE = "index.json";
static constexpr int INDEX_WRITE_DELAY_MS = 2000;

TrackCache::TrackCache(QDir cache_path, qint64 budget, QObject *parent)
  : QObject{parent}
  , dir{cache_path.filePath("tracks")}
  , budget_bytes{budget}
{
  if (!dir.exists() && !dir.mkpath(dir.path()))
    {
      spdlog::warn("TrackCache: couldn't make cache path [{}] — caching off",
          dir.path());
      usable = false;
      return;
    }

  index_writer.setSingleShot(true);
  index_writer.setInterval(INDEX_WRITE_DELAY_MS);
  connect(&index_writer, &QTimer::timeout, this, &TrackCache::save_index);

  load_index();
  evict_to_budget();
}

TrackCache::~TrackCache()
{
  if (index_writer.isActive()) save_index();
}

auto
TrackCache::key_for(const QString &track_id) const -> QString
{
  QCryptographicHash h{QCryptographicHash::Sha256};
  h.addData(QByteArray{KEY_VERSION});
  h.addData(QByteArrayView{"\0", 1});
  h.addData(cache_scope.toUtf8());
  h.addData(QByteArrayView{"\0", 1});
  h.addData(track_id.toUtf8());
  return QString::fromLatin1(h.result().toHex().left(32));
}

auto
TrackCache::complete_path(const QString &track_id) const -> QString
{
  return path_for_key(key_for(track_id));
}

auto
TrackCache::path_for_key(const QString &key) const -> QString
{
  return dir.filePath(key + ".trk");
}

auto
TrackCache::part_path(const QString &track_id) const -> QString
{
  return dir.filePath(key_for(track_id) + ".part");
}

// ---------------------------------------------------------------------------
// Index, budget, eviction
// ---------------------------------------------------------------------------

void
TrackCache::load_index()
{
  QFile f{dir.filePath(INDEX_FILE)};
  if (f.open(QIODevice::ReadOnly))
    {
      const auto doc = QJsonDocument::fromJson(f.readAll());
      for (const auto &v : doc["entries"].toArray())
        {
          const auto o = v.toObject();
          const auto key = o["key"].toString();
          if (key.isEmpty()) continue;
          index.insert(key,
              Entry{static_cast<qint64>(o["size"].toDouble()),
                  static_cast<qint64>(o["used"].toDouble())});
        }
    }

  // The files are the truth; the index only remembers when each was last
  // wanted. Anything it claims that isn't on disk goes, and anything on disk it
  // doesn't know about is adopted at its mtime — so a lost or corrupt index
  // costs eviction ordering, not the cache.
  bool changed = false;
  const auto present = dir.entryInfoList({"*.trk"}, QDir::Files);
  QSet<QString> on_disk;
  for (const auto &fi : present)
    {
      const QString key = fi.completeBaseName();
      on_disk.insert(key);
      auto it = index.find(key);
      if (it == index.end())
        {
          index.insert(key,
              Entry{fi.size(), fi.lastModified().toMSecsSinceEpoch()});
          changed = true;
        }
      else if (it->size != fi.size())
        {
          it->size = fi.size();
          changed = true;
        }
    }
  for (auto it = index.begin(); it != index.end();)
    {
      if (on_disk.contains(it.key()))
        ++it;
      else
        {
          it = index.erase(it);
          changed = true;
        }
    }

  // Partial downloads do not survive a restart. Nothing can be in flight here,
  // so every one of them is an orphan, and the alternative — keeping them for a
  // resume that may never come — is an unbounded pile of bytes no budget counts.
  for (const auto &fi : dir.entryInfoList({"*.part"}, QDir::Files))
    QFile::remove(fi.absoluteFilePath());

  for (const auto &e : index)
    last_use_stamp = std::max(last_use_stamp, e.used);

  if (changed) save_index();
}

void
TrackCache::save_index()
{
  if (!usable) return;

  QJsonArray entries;
  for (auto it = index.constBegin(); it != index.constEnd(); ++it)
    {
      QJsonObject o;
      o["key"] = it.key();
      o["size"] = static_cast<double>(it->size);
      o["used"] = static_cast<double>(it->used);
      entries.append(o);
    }

  QJsonObject root;
  root["version"] = 1;
  root["entries"] = entries;

  QSaveFile f{dir.filePath(INDEX_FILE)};
  if (!f.open(QIODevice::WriteOnly)
      || f.write(QJsonDocument{root}.toJson(QJsonDocument::Compact)) < 0
      || !f.commit())
    spdlog::debug("TrackCache: couldn't write the index — eviction order will "
                  "be rebuilt from mtimes");
}

auto
TrackCache::next_use_stamp() -> qint64
{
  // Strictly increasing, rather than simply the clock: two uses inside the same
  // millisecond have an order, and eviction has to honour it or it evicts by
  // hash order among the ties. The value stays a wall-clock time to the eye,
  // which is what keeps the index readable.
  last_use_stamp = std::max(QDateTime::currentMSecsSinceEpoch(), last_use_stamp + 1);
  return last_use_stamp;
}

void
TrackCache::touch(const QString &key)
{
  auto it = index.find(key);
  if (it == index.end()) return;
  it->used = next_use_stamp();
  index_writer.start();
}

auto
TrackCache::bytes_used() const -> qint64
{
  qint64 total = 0;
  for (const auto &e : index)
    total += e.size;
  return total;
}

void
TrackCache::set_budget(qint64 bytes)
{
  if (budget_bytes == bytes) return;
  budget_bytes = bytes;
  evict_to_budget();
}

void
TrackCache::set_enabled(bool on)
{
  if (enabled == on) return;
  enabled = on;
  // Cached files stay: switching the cache off is not a request to delete
  // anything, and Clear exists for when it is.
  if (!enabled) abandon_fetch(false);
}

void
TrackCache::set_pinned(const QStringList &track_ids)
{
  pinned.clear();
  for (const auto &id : track_ids)
    if (!id.isEmpty()) pinned.insert(key_for(id));
}

void
TrackCache::evict_to_budget()
{
  if (!usable || budget_bytes <= 0) return;

  qint64 total = bytes_used();
  if (total <= budget_bytes) return;

  auto keys = index.keys();
  std::ranges::sort(keys, [this](const QString &a, const QString &b) {
    return index[a].used < index[b].used;
  });

  for (const auto &key : keys)
    {
      if (total <= budget_bytes) break;
      if (pinned.contains(key)) continue;

      const qint64 size = index[key].size;
      if (!QFile::remove(path_for_key(key)))
        spdlog::debug("TrackCache: couldn't evict [{}]", key);
      index.remove(key);
      total -= size;
    }

  if (total > budget_bytes)
    spdlog::debug("TrackCache: {} bytes over a {} byte budget, and the rest is "
                  "playing or armed to play",
        total - budget_bytes, budget_bytes);

  index_writer.start();
}

void
TrackCache::clear()
{
  abandon_fetch(true);
  pending_id.clear();
  pending_url.clear();

  for (const auto &fi : dir.entryInfoList({"*.trk", "*.part"}, QDir::Files))
    QFile::remove(fi.absoluteFilePath());

  index.clear();
  id_by_path.clear();
  save_index();
  index_writer.stop();
}

// ---------------------------------------------------------------------------

void
TrackCache::set_scope(const QString &profile_scope)
{
  if (cache_scope == profile_scope) return;

  // Anything in flight or queued was keyed under the outgoing scope. Dropping
  // the queued one first matters: abandoning a fetch runs the queue. Partial
  // files stay on disk under the old key, so switching back resumes them.
  pending_id.clear();
  pending_url.clear();
  abandon_fetch(false);
  cache_scope = profile_scope;
}

auto
TrackCache::local_for(const QString &track_id) -> QUrl
{
  if (!usable || !enabled || track_id.isEmpty()) return {};

  const QString path = complete_path(track_id);
  if (!QFileInfo::exists(path)) return {};

  touch(key_for(track_id));
  id_by_path.insert(path, track_id);
  return QUrl::fromLocalFile(path);
}

auto
TrackCache::id_for_local(const QUrl &local) const -> QString
{
  if (!local.isLocalFile()) return {};
  return id_by_path.value(local.toLocalFile());
}

void
TrackCache::warm(const QString &track_id, const QUrl &remote)
{
  if (!usable || !enabled || track_id.isEmpty() || !remote.isValid()) return;
  if (QFileInfo::exists(complete_path(track_id))) return;
  if (active_id == track_id) return;

  if (held)
    {
      pending_id = track_id;
      pending_url = remote;
      return;
    }

  // Whatever was being fetched is no longer what plays next.
  abandon_fetch(false);
  start_fetch(track_id, remote);
}

void
TrackCache::set_held(bool hold)
{
  if (held == hold) return;
  held = hold;

  if (held)
    {
      if (reply)
        {
          pending_id = active_id;
          pending_url = active_url;
          abandon_fetch(false);
          spdlog::debug("TrackCache: prefetch of [{}] suspended for playback",
              pending_id);
        }
      return;
    }

  start_pending();
}

void
TrackCache::start_pending()
{
  if (held || pending_id.isEmpty()) return;
  const QString id = std::exchange(pending_id, QString{});
  const QUrl url = std::exchange(pending_url, QUrl{});
  warm(id, url);
}

void
TrackCache::start_fetch(const QString &track_id, const QUrl &remote)
{
  active_id = track_id;
  active_url = remote;
  header_checked = false;
  discard_part = false;
  expected_size = -1;

  part.setFileName(part_path(track_id));
  part_offset = part.exists() ? part.size() : 0;

  const bool opened = part_offset > 0
                          ? part.open(QIODevice::Append)
                          : part.open(QIODevice::WriteOnly | QIODevice::Truncate);
  if (!opened)
    {
      spdlog::warn("TrackCache: can't write [{}] — caching this track off",
          part.fileName());
      clear_fetch_state();
      return;
    }

  QNetworkRequest req{remote};
  if (part_offset > 0)
    req.setRawHeader("Range",
        QByteArray{"bytes="} + QByteArray::number(part_offset) + "-");

  reply = net.get(req);
  connect(reply, &QNetworkReply::readyRead, this, &TrackCache::on_ready_read);
  connect(reply, &QNetworkReply::finished, this, &TrackCache::on_finished);

  spdlog::debug("TrackCache: fetching [{}] from byte {}", track_id, part_offset);
}

void
TrackCache::abandon_fetch(bool discard)
{
  if (!reply) return;
  discard_part = discard;
  reply->abort(); // emits finished synchronously; on_finished tidies up
}

void
TrackCache::on_ready_read()
{
  if (!reply) return;

  if (!header_checked)
    {
      header_checked = true;

      const int status
          = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      if (status != 200 && status != 206)
        {
          spdlog::debug("TrackCache: [{}] refused with HTTP {}", active_id, status);
          abandon_fetch(true); // an error body is not audio
          return;
        }

      // A server that ignores the range answers 200 with the whole file, which
      // would otherwise be appended to the bytes already held.
      if (part_offset > 0 && status != 206)
        {
          part.close();
          if (!part.open(QIODevice::WriteOnly | QIODevice::Truncate))
            {
              abandon_fetch(true);
              return;
            }
          part_offset = 0;
        }

      const auto len = reply->header(QNetworkRequest::ContentLengthHeader);
      if (len.isValid())
        expected_size = part_offset + len.toLongLong();
    }

  const QByteArray chunk = reply->readAll();
  if (chunk.isEmpty()) return;

  // Flushed as it arrives: the partial file is the record a resume reads, and
  // bytes still sitting in a buffer when the process dies are bytes downloaded
  // again.
  if (part.write(chunk) != chunk.size() || !part.flush())
    {
      spdlog::warn("TrackCache: write to [{}] failed — dropping it",
          part.fileName());
      abandon_fetch(true);
    }
}

void
TrackCache::on_finished()
{
  if (!reply) return;

  QNetworkReply *r = std::exchange(reply, nullptr);
  r->deleteLater();

  const QString id = active_id;
  part.close();

  // A reply with no body never reaches on_ready_read, so the answer has to be
  // read here too — a 404 must not leave an empty file that looks resumable.
  const int status
      = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const bool refused = status != 0 && status != 200 && status != 206;
  const bool failed = r->error() != QNetworkReply::NoError || discard_part || refused;

  if (failed)
    {
      // The bytes already written are kept, and the next attempt asks for the
      // rest of them — unless the failure is one a resume can't fix, or there
      // is nothing on disk worth resuming from.
      if (discard_part || refused || part.size() == 0) part.remove();
      clear_fetch_state();
      start_pending();
      return;
    }

  if (expected_size >= 0 && part.size() != expected_size)
    {
      spdlog::debug("TrackCache: [{}] ended short ({} of {} bytes)", id,
          part.size(), expected_size);
      clear_fetch_state();
      start_pending();
      return;
    }

  const QString final_path = complete_path(id);
  QFile::remove(final_path); // rename won't replace an existing file
  if (!part.rename(final_path))
    {
      spdlog::warn("TrackCache: couldn't move [{}] into place", id);
      clear_fetch_state();
      start_pending();
      return;
    }

  const qint64 size = QFileInfo{final_path}.size();
  const QString key = key_for(id);
  clear_fetch_state();

  index.insert(key, Entry{size, next_use_stamp()});
  index_writer.start();
  // Before the announcement, not after: a budget this file alone cannot fit in
  // would otherwise have the engine arm a path that is about to be deleted.
  evict_to_budget();

  if (!QFileInfo::exists(final_path))
    {
      spdlog::debug("TrackCache: [{}] evicted as soon as it landed — it does "
                    "not fit the budget",
          id);
      start_pending();
      return;
    }

  id_by_path.insert(final_path, id);
  spdlog::debug("TrackCache: cached [{}] ({} bytes)", id, size);
  emit warmed(id, QUrl::fromLocalFile(final_path));

  start_pending();
}

void
TrackCache::clear_fetch_state()
{
  active_id.clear();
  active_url.clear();
  part_offset = 0;
  expected_size = -1;
  header_checked = false;
  discard_part = false;
}
