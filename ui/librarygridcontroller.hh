#ifndef LIBRARYGRIDCONTROLLER_HH
#define LIBRARYGRIDCONTROLLER_HH

#include <QObject>
#include <QString>
#include <QTimer>

#include <memory>

#include "aclistmodel.hh" // AlbumCoverListModel
#include "acsortmodel.hh" // AlbumCoverSortModel
#include "model/album.hh" // album
#include "model/song.hh"  // song

class QModelIndex;
class QItemSelection;
class QPushButton;
class QLineEdit;
class QLabel;
class QWidget;
class AlbumCoverListView;
class AlbumView;
class EmptyLibraryView;
class BackendController;
class AlbumArtManager;

// Owns the library content area extracted from MainWindow: the album grid models
// (source + sort/filter proxy), grid activation/selection/prefetch, the open-album
// AlbumView and its tracklist ops, the All/Recent toggle with per-view scroll
// memory, the search filter, the album count label, the empty-state placeholder,
// and scroll-to-playing (Ctrl+L + startup). MainWindow constructs the .ui children,
// AlbumView and EmptyLibraryView, then passes them here; it keeps top-level view
// navigation and the now-playing/playback art feed.
class LibraryGridController : public QObject {
  Q_OBJECT

public:
  LibraryGridController(AlbumCoverListView *albumlist, QPushButton *recentbutton,
      QPushButton *allbutton, QLineEdit *searchbox, QLabel *library_title,
      QLabel *album_count_label, QWidget *library_toolbar, QWidget *library_content,
      AlbumView *albumview, EmptyLibraryView *empty_library_view,
      BackendController *backend, std::shared_ptr<AlbumArtManager> artman,
      QObject *parent = nullptr);

  // Number of albums in the library (source model), for the connection-state
  // controller's overlay-vs-empty decision.
  auto album_count() const -> int;
  // The current library albums, for the shell's thumbnail rebuild.
  auto albums() const -> QList<album>;

  // Fed from MainWindow::current_song_changed: drives startup scroll + Ctrl+L.
  auto set_current_album(const QString &hash) -> void;
  // Fed from MainWindow::playback_state_updated, for the "insert after current" ops.
  auto set_current_queue_pos(int pos) -> void;

  // Close the open album (BackButton in MainWindow's event filter routes here).
  auto albumview_hide() -> void;

public slots:
  void reload();            // backend->library_changed
  void scroll_to_playing(); // Ctrl+L action

signals:
  // Emitted on every library load; the shell calls notify_library_loaded() and
  // kicks the thumbnail rebuild + now-playing art refresh.
  void library_loaded();
  // scroll_to_playing() needs the library view foregrounded; the shell owns nav.
  void show_library_requested();

private:
  void setup_connections();
  void albumlist_activated(const QModelIndex &index);
  void albumlist_selection_changed(const QItemSelection &selected, const QItemSelection &deselected);
  void songlist_activated(uint32_t pos, QList<song> songs);
  void queue_songs(const QList<song> &songs);
  void albumview_show();
  void recent_toggled(bool checked);
  void update_album_count();
  void update_empty_state();
  void maybe_startup_scroll();
  // Empty the grid without the library_loaded side effects reload() carries.
  auto clear_grid() -> void;

  AlbumCoverListView *albumlist;
  QPushButton *recentbutton;
  QPushButton *allbutton;
  QLineEdit *searchbox;
  QLabel *library_title;
  QLabel *album_count_label;
  QWidget *library_toolbar;
  QWidget *library_content;
  AlbumView *albumview;
  EmptyLibraryView *empty_library_view;
  BackendController *backend;
  std::shared_ptr<AlbumArtManager> artman;

  std::unique_ptr<AlbumCoverListModel> model;
  std::unique_ptr<AlbumCoverSortModel> proxy_model;

  album open_album;
  QString current_album_hash;
  int last_queue_pos = -1;

  // One-shot: retired the first time the library finishes loading (both backends
  // deliver current_song_changed before the first library_changed at connect, so
  // if a song is already playing it's known by then). Scrolls only if so; never
  // fires again afterward, so playback that starts later in the session (e.g.
  // after a backend switch) doesn't yank the user out of an open AlbumView.
  bool startup_scroll_pending = true;

  // Per-view top-visible row, restored when toggling between All/Recent.
  // [0] = All, [1] = Recent. Stored as a row (not a pixel offset) so the
  // restore survives the view's deferred relayout / scroll-range change.
  int view_top_row[2] = {0, 0};

  // Warm the song cache for the selected album so the tracklist is ready by
  // the time it's opened; debounced so arrow-key scrolling doesn't spam fetches.
  QTimer prefetch_timer;
  album prefetch_album;
};

// Library toolbar text, factored out so the pluralization + Recent/All wording is
// unit-testable without building the controller.
namespace librarygrid {
auto title_text(bool recent) -> QString;
auto count_label(int n, bool recent) -> QString;
}

#endif // LIBRARYGRIDCONTROLLER_HH
