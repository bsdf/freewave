#ifndef FAVORITESMANAGER_HH
#define FAVORITESMANAGER_HH

#include <memory>

#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

#include "controller/backend.hh"

// In-memory cache of the favorites — the UI's single source of truth for both
// the hearts (the uri set) and the auto "Favorited Tracks" list (the songs).
// Parallels LibraryManager, but is a QObject so views can react to changes. The
// backend (the BackendController façade) remains the persistent source of
// truth; this reconciles from its favorites_loaded / favorite_songs_loaded /
// favorite_changed signals and pushes toggles down optimistically.
class FavoritesManager : public QObject {
  Q_OBJECT
public:
  explicit FavoritesManager(std::shared_ptr<Backend> backend,
      QObject *parent = nullptr);

  auto is_favorite(const QString &uri) const -> bool; // synchronous — for paint
  auto all() const -> QSet<QString>;                  // for a future favorites view
  auto available() const -> bool;                     // backend supports favorites
  // The favorited songs behind the auto list, in server order. Empty until the
  // first fetch answers, which songs_loaded() distinguishes from "no favorites".
  auto songs() const -> QList<song>;
  auto songs_loaded() const -> bool;

public slots:
  void toggle(const QString &uri);
  void set_favorite(const QString &uri, bool fav);
  // Read the favorites from the server. The only place that happens: views ask
  // for it rather than calling the backend, and the manager is what knows
  // whether the backend can answer. Reasons it runs are connect, the backend
  // reporting the server may have diverged (favorites_stale), a post-write
  // confirmation, and the corrective read after a failed toggle.
  void request_refresh();

signals:
  void favorite_changed(const QString &uri, bool fav); // one item — targeted repaint
  void favorites_reset();                              // bulk reload — repaint all
  void songs_changed();                                // the auto list's contents

private:
  QSet<QString> favs;
  QList<song> fav_songs;
  bool fav_songs_loaded = false;
  std::shared_ptr<Backend> backend;
  // A toggle was pushed to the backend and not yet reconciled. If the backend
  // then reports an error, the optimistic change may not have persisted, so we
  // re-fetch the authoritative set to correct it.
  bool pending = false;
};

#endif // FAVORITESMANAGER_HH
