#ifndef TRACKCACHE_HH
#define TRACKCACHE_HH

#include <QDir>
#include <QFile>
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>

class QNetworkReply;

// An on-disk cache of whole audio files, so that a track played a second time is
// read from the local filesystem instead of streamed. A deep seek into a cold
// remote FLAC costs ten seconds or more and no client-side flag changes that;
// local bytes do.
//
// The cache never gates playback. A hit yields a file:// URL, a miss yields
// nothing and the caller streams exactly as it does without a cache. Only
// complete files are ever served: a download lands in `<key>.part` and is
// renamed into place, so nothing hands a growing file to the decoder.
//
// One fetch runs at a time — the only thing worth prefetching is the track that
// plays next — and it yields to playback. A prefetch shares the link with the
// stream, and a concurrent full-file GET was measured tripling the latency of a
// deep seek on the same connection, so `set_held` suspends the fetch while the
// player is doing something the listener is waiting on. A suspended fetch keeps
// its partial file and resumes with a range request, so repeated seeks cost
// nothing beyond the delay.
//
// It stays under a byte budget by evicting least-recently-used entries, and it
// never evicts what is playing or armed to play next (see `set_pinned`), so the
// budget can be exceeded by at most those. Last-used times are kept in a JSON
// sidecar rather than read from the filesystem, because `relatime` makes atime
// unreliable; the sidecar is rebuildable by directory scan and is never the
// authority on whether a file exists.
//
// GStreamer-free by construction: it speaks HTTP and the filesystem, and is
// testable against a plain local HTTP server.
class TrackCache : public QObject {
  Q_OBJECT
public:
  // cache_path is the application cache root; the tracks live in a subdirectory
  // of it. A root that cannot be created disables the cache — every call then
  // behaves as a miss, which is playback without a cache. budget_bytes <= 0
  // means no limit, which nothing in the app configures.
  explicit TrackCache(QDir cache_path, qint64 budget_bytes,
      QObject *parent = nullptr);
  ~TrackCache() override;

  // Namespace the cache to the active server profile. Two profiles on the same
  // server duplicate bytes; proving they point at one library is not worth the
  // machinery. Changing scope abandons anything in flight.
  void set_scope(const QString &profile_scope);
  auto scope() const -> QString { return cache_scope; }

  // Off, every call behaves as a miss and nothing is fetched. Cached files are
  // left alone: turning the cache off is not a request to delete anything.
  void set_enabled(bool on);
  auto is_enabled() const -> bool { return enabled; }

  void set_budget(qint64 bytes);
  auto budget() const -> qint64 { return budget_bytes; }
  auto bytes_used() const -> qint64;

  // The local file for a track, or an empty URL if it is not cached whole.
  // A hit counts as a use, which is what keeps a track being played out of the
  // way of eviction.
  auto local_for(const QString &track_id) -> QUrl;

  // Fetch `remote` into the cache unless it is already there or already in
  // flight. Replaces any other fetch: the successor this was called for has
  // changed, so the old one is no longer what plays next.
  void warm(const QString &track_id, const QUrl &remote);

  // The tracks that must survive eviction — what is playing and what is armed
  // to play next. Passed as a set rather than toggled one at a time because
  // that is what it is: two ids that change together, and a replace can't leak
  // a pin the way a missed unpin can.
  void set_pinned(const QStringList &track_ids);

  // The track id behind a file:// URL this cache handed out. A stream URL
  // carries the track id in its query and a local path does not, so a caller
  // that identifies tracks by URL needs this to close the loop.
  auto id_for_local(const QUrl &local) const -> QString;

  // Suspend fetching while the player needs the link. Held, an in-flight fetch
  // is abandoned and re-queued; releasing resumes it from the bytes already on
  // disk.
  void set_held(bool held);
  auto is_held() const -> bool { return held; }

  // Whether a fetch is running right now (in flight, not merely queued).
  auto is_fetching() const -> bool { return reply != nullptr; }

  // Delete everything, including anything in flight. Pins do not protect
  // against this: it is an explicit instruction, not eviction.
  void clear();

signals:
  // A track has just landed on disk whole. `local` is the file:// URL for it.
  void warmed(const QString &track_id, const QUrl &local);

private:
  struct Entry {
    qint64 size = 0;
    qint64 used = 0; // ms since the epoch — seconds tie for tracks queued together
  };

  auto key_for(const QString &track_id) const -> QString;
  auto complete_path(const QString &track_id) const -> QString;
  auto part_path(const QString &track_id) const -> QString;
  auto path_for_key(const QString &key) const -> QString;

  void start_fetch(const QString &track_id, const QUrl &remote);
  // Run the fetch that was queued while the link was held, if any.
  void start_pending();
  // Stop the running fetch. `discard` also deletes the partial file, for a
  // failure that resuming cannot fix (a refused request, a bad write).
  void abandon_fetch(bool discard);
  void on_ready_read();
  void on_finished();
  void clear_fetch_state();

  void load_index();
  void save_index();
  // Strictly increasing use order — see the definition for why not the clock.
  auto next_use_stamp() -> qint64;
  void touch(const QString &key);
  // Drop least-recently-used entries until the budget is met. Pinned entries
  // are never dropped, so a budget smaller than what is pinned is simply not
  // met — the alternative is deleting the file that is playing.
  void evict_to_budget();

  QDir dir;
  QString cache_scope;
  bool usable = true;
  bool enabled = true;
  bool held = false;
  qint64 budget_bytes = 0;

  QNetworkAccessManager net;

  // The fetch in flight, and the one waiting for the link to be free again.
  QString active_id;
  QNetworkReply *reply = nullptr;
  QFile part;
  qint64 part_offset = 0; // bytes already on disk when this fetch started
  qint64 expected_size = -1;
  bool header_checked = false;
  bool discard_part = false;

  QString pending_id;
  QUrl pending_url;
  QUrl active_url;

  QHash<QString, Entry> index; // cache key -> size + last use
  qint64 last_use_stamp = 0;
  QSet<QString> pinned; // cache keys, not track ids
  QTimer index_writer;  // coalesces index writes; a hit is not a reason to hit the disk

  // Reverse map for id_for_local, filled whenever a local URL is handed out.
  // The engine can only report a file:// URL that came from here, so an entry
  // exists by the time anything asks for it.
  QHash<QString, QString> id_by_path;
};

#endif // TRACKCACHE_HH
