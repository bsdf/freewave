#include "librarygridcontroller.hh"

#include <QItemSelection>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPersistentModelIndex>
#include <QPushButton>
#include <QTimer>
#include <QWidget>

#include "aclistview.hh"
#include "albumview.hh"
#include "controller/backendcontroller.hh"
#include "controller/settings.hh"
#include "emptylibraryview.hh"

auto
librarygrid::title_text(bool recent) -> QString
{
  return recent ? "Recently added" : "Library";
}

auto
librarygrid::count_label(int n, bool recent) -> QString
{
  const char *sort = recent ? "sorted by added" : "sorted by artist";
  return QString("%1 album%2 ～ %3").arg(n).arg(n == 1 ? "" : "s").arg(sort);
}

LibraryGridController::LibraryGridController(AlbumCoverListView *albumlist,
    QPushButton *recentbutton, QPushButton *allbutton, QLineEdit *searchbox,
    QLabel *library_title, QLabel *album_count_label, QWidget *library_toolbar,
    QWidget *library_content, AlbumView *albumview,
    EmptyLibraryView *empty_library_view, BackendController *backend,
    std::shared_ptr<AlbumArtManager> artman, QObject *parent)
  : QObject(parent)
  , albumlist(albumlist)
  , recentbutton(recentbutton)
  , allbutton(allbutton)
  , searchbox(searchbox)
  , library_title(library_title)
  , album_count_label(album_count_label)
  , library_toolbar(library_toolbar)
  , library_content(library_content)
  , albumview(albumview)
  , empty_library_view(empty_library_view)
  , backend(backend)
  , artman(std::move(artman))
{
  model = std::make_unique<AlbumCoverListModel>(this->artman);
  proxy_model = std::make_unique<AlbumCoverSortModel>();
  proxy_model->setSourceModel(model.get());
  albumlist->setModel(proxy_model.get());

  setup_connections();

  // Apply the saved All/Recent state (wired above, so this drives the filter).
  if (Settings().ui_recent())
    recentbutton->setChecked(true);
}

auto
LibraryGridController::setup_connections() -> void
{
  connect(backend, &Backend::library_changed,
      this, &LibraryGridController::reload);

  // The open album (if any) belongs to the outgoing server; its hash/native id
  // won't resolve against the new one, so drop back to the grid — and empty the
  // grid with it. The library cache behind these cells is cleared by the switch,
  // and a switch that fails to connect never reloads them, so anything left on
  // screen is the old server's catalog over a library that can no longer answer
  // for it.
  connect(backend, &BackendController::backend_switching, this, [this] {
    albumview_hide();
    clear_grid();
  });

  // Grid: activation opens the album (data + skeleton, then fetch) and shows it;
  // selection warms the song cache.
  connect(albumlist, &AlbumCoverListView::activated,
      this, &LibraryGridController::albumlist_activated);
  connect(albumlist, &AlbumCoverListView::activated,
      this, &LibraryGridController::albumview_show);
  connect(albumlist->selectionModel(), &QItemSelectionModel::selectionChanged,
      this, &LibraryGridController::albumlist_selection_changed);

  // Grid context-menu album ops.
  connect(albumlist, &AlbumCoverListView::play_albums,
      this, [this](const QList<album> &a) { backend->replace_albums(a); });
  connect(albumlist, &AlbumCoverListView::play_albums_next,
      this, [this](const QList<album> &a) { backend->insert_albums(a, last_queue_pos + 1); });
  connect(albumlist, &AlbumCoverListView::queue_albums,
      this, [this](const QList<album> &a) { backend->append_albums(a); });

  prefetch_timer.setSingleShot(true);
  prefetch_timer.setInterval(120);
  connect(&prefetch_timer, &QTimer::timeout, this, [this] {
    if (!prefetch_album.album_hash.isEmpty())
      backend->fetch_songs(prefetch_album, {}); // warm libman cache
  });

  // Open-album view: tracklist ops + close.
  connect(albumview, &AlbumView::close,
      this, &LibraryGridController::albumview_hide);
  connect(albumview, &AlbumView::activated,
      this, &LibraryGridController::songlist_activated);
  connect(albumview, &AlbumView::play_songs_next,
      this, [this](const QList<song> &s) { backend->insert_queue(s, last_queue_pos + 1); });
  connect(albumview, &AlbumView::queue_songs,
      this, &LibraryGridController::queue_songs);
  connect(backend, &Backend::current_song_changed,
      albumview, &AlbumView::set_current_song);

  // Empty-state refresh button.
  connect(empty_library_view, &EmptyLibraryView::refresh,
      this, [this] { backend->refresh_library(); });

  // All/Recent toggle.
  connect(recentbutton, &QPushButton::toggled,
      this, &LibraryGridController::recent_toggled);

  // Search filter (the field's chrome / focus behavior stays in MainWindow).
  connect(searchbox, &QLineEdit::textChanged,
      proxy_model.get(), &AlbumCoverSortModel::set_search_filter);

  // Album count label tracks the filtered proxy.
  connect(proxy_model.get(), &QAbstractItemModel::modelReset,
      this, &LibraryGridController::update_album_count);
  connect(proxy_model.get(), &QAbstractItemModel::rowsInserted,
      this, [this](const QModelIndex &, int, int) { update_album_count(); });
  connect(proxy_model.get(), &QAbstractItemModel::rowsRemoved,
      this, [this](const QModelIndex &, int, int) { update_album_count(); });
}

auto
LibraryGridController::album_count() const -> int
{
  return static_cast<int>(model->get_data().size());
}

auto
LibraryGridController::albums() const -> QList<album>
{
  return model->get_data();
}

auto
LibraryGridController::set_current_album(const QString &hash) -> void
{
  current_album_hash = hash;
  maybe_startup_scroll();
}

auto
LibraryGridController::set_current_queue_pos(int pos) -> void
{
  last_queue_pos = pos;
}

auto
LibraryGridController::clear_grid() -> void
{
  model->set_data({});
  update_empty_state();
}

auto
LibraryGridController::reload() -> void
{
  auto albums = backend->get_albums();

  // Save current selection by hash so we can restore it after the model reset.
  QString saved_hash;
  auto cur = albumlist->selectionModel()->currentIndex();
  if (cur.isValid())
    saved_hash = cur.data(AlbumCoverListModel::UserRoleAlbum).value<album>().album_hash;

  model->set_data(albums);

  if (!saved_hash.isEmpty())
    {
      for (int i = 0; i < proxy_model->rowCount(); ++i)
        {
          auto idx = proxy_model->index(i, 0);
          if (idx.data(AlbumCoverListModel::UserRoleAlbum).value<album>().album_hash == saved_hash)
            {
              albumlist->selectionModel()->setCurrentIndex(
                  idx, QItemSelectionModel::SelectCurrent);
              albumlist->scrollTo(idx, QAbstractItemView::EnsureVisible);
              break;
            }
        }
    }

  // The shell tears down the startup loading overlay and kicks the art fetch.
  emit library_loaded();

  update_empty_state();
  maybe_startup_scroll();
}

auto
LibraryGridController::maybe_startup_scroll() -> void
{
  if (!startup_scroll_pending || proxy_model->rowCount() == 0)
    return;
  startup_scroll_pending = false;
  if (!current_album_hash.isEmpty())
    scroll_to_playing();
}

auto
LibraryGridController::scroll_to_playing() -> void
{
  if (current_album_hash.isEmpty())
    return;

  if (albumview->isVisible())
    albumview_hide();
  emit show_library_requested();

  auto find_index = [this] {
    for (int i = 0; i < proxy_model->rowCount(); ++i)
      {
        auto idx = proxy_model->index(i, 0);
        if (idx.data(AlbumCoverListModel::UserRoleAlbum).value<album>().album_hash
            == current_album_hash)
          return idx;
      }
    return QModelIndex();
  };

  auto idx = find_index();
  // The playing album may sit outside the Recent filter; fall back to All.
  if (!idx.isValid() && recentbutton->isChecked())
    {
      allbutton->setChecked(true);
      idx = find_index();
    }
  if (!idx.isValid())
    return;

  albumlist->selectionModel()->setCurrentIndex(
      idx, QItemSelectionModel::SelectCurrent);
  albumlist->setFocus();
  // Deferred so it runs after any pending per-view scroll restore queued by a
  // recent_toggled() above (singleShot callbacks fire in registration order).
  QPersistentModelIndex pidx(idx);
  QTimer::singleShot(0, albumlist, [this, pidx] {
    if (pidx.isValid())
      albumlist->scrollTo(pidx, QAbstractItemView::PositionAtCenter);
  });
}

auto
LibraryGridController::albumlist_activated(const QModelIndex &index) -> void
{
  open_album = index.data(AlbumCoverListModel::UserRoleAlbum).value<album>();
  auto a = open_album;
  albumview->set_data(a, {}); // immediate: header + skeleton rows
  backend->fetch_songs(a, [this, a](const QList<song> &songs) {
    if (open_album.album_hash == a.album_hash)
      albumview->set_data(a, songs); // fill once loaded
  });
}

auto
LibraryGridController::albumlist_selection_changed(const QItemSelection &selected,
    const QItemSelection &) -> void
{
  auto idxs = selected.indexes();
  if (idxs.isEmpty())
    return;

  // Warm the song cache for the newly selected album. Debounced so holding an
  // arrow key doesn't fire a request per row — only the album we settle on.
  prefetch_album = idxs.first().data(AlbumCoverListModel::UserRoleAlbum).value<album>();
  prefetch_timer.start();
}

auto
LibraryGridController::songlist_activated(uint32_t pos, QList<song> songs) -> void
{
  auto selected_song = songs[pos];
  spdlog::trace("clicked song in songlist = [{}]", selected_song.uri);

  backend->replace_queue(songs, pos);
}

auto
LibraryGridController::queue_songs(const QList<song> &songs) -> void
{
  backend->append_queue(songs);
}

auto
LibraryGridController::albumview_show() -> void
{
  albumview->setVisible(true);
  albumlist->setVisible(false);
  library_toolbar->setVisible(false);
}

auto
LibraryGridController::albumview_hide() -> void
{
  albumview->setVisible(false);
  albumlist->setVisible(true);
  library_toolbar->setVisible(true);
  // Restore the empty-state placeholder if we returned to an empty library.
  update_empty_state();
}

auto
LibraryGridController::recent_toggled(bool checked) -> void
{
  // Remember the top-visible row of the view we're leaving so returning to it
  // restores the spot. A row survives the filter swap; a pixel offset wouldn't.
  auto top = albumlist->indexAt(QPoint(0, 0));
  view_top_row[checked ? 0 : 1] = top.isValid() ? top.row() : 0;

  if (checked)
    {
      // TODO: make what "recent" means configurable.
      // proxy_model->set_date_filter(QDateTime::currentDateTime().addMonths(-1));
      proxy_model->set_most_recent(100);
    }
  else
    {
      // proxy_model->clear_date_filter();
      proxy_model->clear_most_recent();
    }
  Settings().set_ui_recent(checked);
  update_album_count();

  // Deferred so the model's pending relayout has settled; scrollTo() forces the
  // layout it needs, so the target row lands at the top regardless of range.
  int row = view_top_row[checked ? 1 : 0];
  QTimer::singleShot(0, albumlist, [this, row] {
    if (row > 0 && row < proxy_model->rowCount())
      albumlist->scrollTo(proxy_model->index(row, 0),
          QAbstractItemView::PositionAtTop);
    else
      albumlist->scrollToTop();
  });
}

auto
LibraryGridController::update_album_count() -> void
{
  const bool recent = recentbutton->isChecked();
  library_title->setText(librarygrid::title_text(recent));
  album_count_label->setText(librarygrid::count_label(proxy_model->rowCount(), recent));
}

auto
LibraryGridController::update_empty_state() -> void
{
  // Base the decision on the source model (the real library), not the filtered
  // proxy — a search that matches nothing must not masquerade as an empty
  // library. Suppressed while an album is open (albumview owns the area then).
  const bool empty = model->rowCount() == 0;
  // isVisibleTo (not isVisible): when Now Playing/Settings hides library_content,
  // an album can still be logically open underneath. isVisible() would report
  // false there and let us force-show albumlist alongside the open albumview,
  // leaving both visible side-by-side on return to the library.
  if (albumview->isVisibleTo(library_content))
    return;
  empty_library_view->setVisible(empty);
  albumlist->setVisible(!empty);
}
