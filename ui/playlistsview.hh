#ifndef PLAYLISTSVIEW_HH
#define PLAYLISTSVIEW_HH

#include <memory>

#include <QListView>
#include <QWidget>

#include "model/song.hh"

class QLabel;
class QListView;
class QPushButton;
class QStackedWidget;
class Backend;
class PlaylistsManager;
class FavoritesManager;
class AlbumArtManager;
class LibraryManager;
class PlaylistSidebarModel;
class PlaylistSidebarItem;
class PlaylistItemModel;
class PlaylistItemDelegate;

// A QListView that hit-tests the per-row favorite heart drawn by
// PlaylistItemDelegate and turns a click on it into favorite_toggled() (rather
// than selecting/activating the row). No drag support yet (reorder is phase 8).
class PlaylistItemView : public QListView {
  Q_OBJECT
public:
  explicit PlaylistItemView(QWidget *parent = nullptr);

  // Enable/disable drag-reorder (off for the read-only auto Favorited Tracks).
  void set_reorderable(bool on);

signals:
  void favorite_toggled(const QString &uri);

protected:
  void mousePressEvent(QMouseEvent *event) override;
};

// The full-content playlists screen: a sidebar of stored playlists + a pinned
// auto "Favorited Tracks" entry, and a detail pane showing the selected
// playlist's contents (album runs coalesced) with Play / Shuffle. Read-only in
// phase 5 — no create/rename/delete/reorder, no add-to-playlist dialog.
class PlaylistsView : public QWidget {
  Q_OBJECT
public:
  PlaylistsView(std::shared_ptr<PlaylistsManager> plman,
      std::shared_ptr<FavoritesManager> favman,
      std::shared_ptr<AlbumArtManager> artman,
      std::shared_ptr<LibraryManager> libman,
      Backend *backend, QWidget *parent = nullptr);
  ~PlaylistsView();

  // Reflect the currently-playing track so the detail list marks it.
  void set_current_uri(const QString &uri);

signals:
  void close_requested();
  void play_songs(uint32_t pos, QList<song> songs); // replace queue, play from pos
  void play_songs_next(QList<song> songs);          // insert after current
  void queue_songs(QList<song> songs);              // append

protected:
  void keyPressEvent(QKeyEvent *event) override;

private:
  void rebuild_sidebar();
  void on_sidebar_selection();
  void load_detail();
  void refresh_items(const QList<song> &songs);
  void update_header();
  void render_header_tile();
  void items_context_menu(const QPoint &pos);
  void create_playlist();
  void rename_current();
  void delete_current();
  void remove_rows(const QModelIndexList &rows);
  auto row_songs(const QModelIndex &index) const -> QList<song>;
  auto is_auto_selected() const -> bool;

  std::shared_ptr<PlaylistsManager> plman;
  std::shared_ptr<FavoritesManager> favman;
  std::shared_ptr<AlbumArtManager> artman;
  std::shared_ptr<LibraryManager> libman;
  Backend *backend;

  QListView *sidebar;
  PlaylistSidebarModel *sidebar_model;
  std::unique_ptr<PlaylistSidebarItem> sidebar_delegate;
  QLabel *sidebar_header;

  QStackedWidget *detail_stack; // 0 = content, 1 = empty (favorites)
  QLabel *tile_label;
  QLabel *eyebrow_label;
  QLabel *title_label;
  QLabel *meta_label;
  QPushButton *play_button;
  QPushButton *shuffle_button;
  QPushButton *more_button;
  PlaylistItemView *items;
  PlaylistItemModel *item_model;
  std::unique_ptr<PlaylistItemDelegate> item_delegate;

  QString current_id; // selected playlist id ("" = auto favorites row)
  bool current_auto = true;
  // Id whose songs/header the detail pane currently reflects ("" = auto row).
  // Distinct from current_id: after our own optimistic reorder the backend's
  // confirm clears the whole song_cache and refetches, re-selecting the *same*
  // playlist with an empty cache. displayed_id == current_id there tells us to
  // keep the shown rows + tile until the refetch lands (the set_data guard then
  // no-ops, since the server order matches the optimistic one) instead of
  // flashing an empty list + placeholder mosaic. On a genuine switch to another
  // playlist the ids differ, so we blank while its contents load.
  QString displayed_id;
  // MPD ids ARE the name, so a rename changes the id — the old current_id
  // vanishes from the refetched list. Remember the requested new name so
  // rebuild_sidebar re-lands the selection on it (Subsonic keeps the id, so the
  // old current_id still resolves; the target is a harmless second lookup there).
  QString pending_rename_target;
  QString current_uri; // now-playing track
  QList<song> current_songs;
};

#endif // PLAYLISTSVIEW_HH
