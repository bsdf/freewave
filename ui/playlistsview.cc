#include "playlistsview.hh"

#include <algorithm>
#include <random>

#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyleOptionViewItem>
#include <QVBoxLayout>

#include "controller/albumartmanager.hh"
#include "controller/backend.hh"
#include "controller/favoritesmanager.hh"
#include "controller/librarymanager.hh"
#include "controller/playlistsmanager.hh"
#include "covermosaic.hh"
#include "playlistitemdelegate.hh"
#include "playlistitemmodel.hh"
#include "playlistsidebaritem.hh"
#include "playlistsidebarmodel.hh"
#include "theme.hh"
#include "timeutil.hh"
#include "ui/theme/components.hh"

namespace {
constexpr int SIDEBAR_W = 248;
constexpr int HEADER_TILE = 132;

// MPD stores playlists as files, so '/' and newline are invalid in a name;
// Subsonic tolerates them but they read as mistakes. Reject empty names too.
auto
valid_playlist_name(const QString &name) -> bool
{
  return !name.isEmpty() && !name.contains('/') && !name.contains('\n');
}
} // namespace

// ── PlaylistItemView ────────────────────────────────────────────────────────

PlaylistItemView::PlaylistItemView(QWidget *parent)
  : QListView(parent)
{
  setMouseTracking(true);
  setDropIndicatorShown(true);
  // Drag-reorder is enabled per-selection (off on the auto Favorited Tracks
  // list) via set_reorderable(); see PlaylistsView::load_detail.
}

void
PlaylistItemView::set_reorderable(bool on)
{
  setDragEnabled(on);
  setAcceptDrops(on);
  setDragDropMode(on ? QAbstractItemView::InternalMove : QAbstractItemView::NoDragDrop);
}

void
PlaylistItemView::mousePressEvent(QMouseEvent *event)
{
  if (event->button() == Qt::LeftButton)
    {
      auto idx = indexAt(event->pos());
      auto *delegate = qobject_cast<PlaylistItemDelegate *>(itemDelegate());
      if (idx.isValid() && delegate)
        {
          QStyleOptionViewItem opt;
          initViewItemOption(&opt);
          opt.rect = visualRect(idx);
          if (delegate->heart_rect(opt, idx).contains(event->pos()))
            {
              auto s = idx.data(PlaylistItemModel::UserRoleSong).value<song>();
              emit favorite_toggled(s.uri);
              event->accept();
              return; // swallow — don't select or activate the row
            }
        }
    }
  QListView::mousePressEvent(event);
}

// ── PlaylistsView ───────────────────────────────────────────────────────────

PlaylistsView::PlaylistsView(std::shared_ptr<PlaylistsManager> plman,
    std::shared_ptr<FavoritesManager> favman,
    std::shared_ptr<AlbumArtManager> artman,
    std::shared_ptr<LibraryManager> libman,
    Backend *backend, QWidget *parent)
  : QWidget(parent)
  , plman{std::move(plman)}
  , favman{std::move(favman)}
  , artman{std::move(artman)}
  , libman{std::move(libman)}
  , backend{backend}
{
  setFocusPolicy(Qt::StrongFocus); // so MainWindow::show_playlists()'s setFocus
                                   // arms the Esc-to-close keyPressEvent handler

  auto *root = new QHBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  // ── Sidebar ──
  auto *sidebar_container = new QWidget(this);
  sidebar_container->setFixedWidth(SIDEBAR_W);
  sidebar_container->setBackgroundRole(QPalette::Base);
  sidebar_container->setAutoFillBackground(true);
  auto *sb_layout = new QVBoxLayout(sidebar_container);
  sb_layout->setContentsMargins(14, 14, 14, 0);
  sb_layout->setSpacing(8);

  auto *back = theme::ui::back_button("Library", sidebar_container);
  connect(back, &QPushButton::clicked, this, [this] { emit close_requested(); });
  // Indent the back button an extra 14px past the sidebar's own 14px margin so
  // its box sits at 28px from the view origin — the canonical back-button
  // placement shared with AlbumView, SettingsView, and NowPlaying.qml — without
  // dragging the "PLAYLISTS" header/list (which stay at the sidebar's 14px).
  auto *back_row = new QHBoxLayout();
  back_row->setContentsMargins(14, 0, 0, 0);
  back_row->addWidget(back, 0, Qt::AlignLeft);
  sb_layout->addLayout(back_row);

  auto *header_row = new QHBoxLayout();
  header_row->setContentsMargins(0, 0, 0, 0);
  sidebar_header = theme::ui::section_header("PLAYLISTS", sidebar_container);
  header_row->addWidget(sidebar_header);
  header_row->addStretch();
  auto *new_button = new QPushButton("＋ New", sidebar_container);
  new_button->setFlat(true);
  new_button->setCursor(Qt::PointingHandCursor);
  new_button->setFont(theme::ui_sans(11));
  new_button->setForegroundRole(QPalette::Highlight);
  connect(new_button, &QPushButton::clicked, this, &PlaylistsView::create_playlist);
  header_row->addWidget(new_button);
  sb_layout->addLayout(header_row);

  sidebar = new QListView(sidebar_container);
  sidebar->setFrameShape(QFrame::NoFrame);
  sidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  sidebar->setSelectionMode(QAbstractItemView::SingleSelection);
  sidebar->setMouseTracking(true);
  {
    auto pal = sidebar->palette();
    pal.setColor(QPalette::Base, Qt::transparent);
    sidebar->setPalette(pal);
  }
  sidebar_model = new PlaylistSidebarModel(this);
  sidebar_model->set_mosaic_resolver([this](const QString &id) -> QStringList {
    return this->plman->mosaic_hashes(id);
  });
  sidebar->setModel(sidebar_model);
  sidebar_delegate = std::make_unique<PlaylistSidebarItem>(this->artman.get());
  sidebar->setItemDelegate(sidebar_delegate.get());
  sb_layout->addWidget(sidebar, 1);

  root->addWidget(sidebar_container);
  root->addWidget(theme::ui::separator(Qt::Vertical, this));

  // ── Detail pane ──
  auto *detail = new QWidget(this);
  auto *dl = new QVBoxLayout(detail);
  dl->setContentsMargins(28, 24, 28, 8);
  dl->setSpacing(14);

  auto *header = new QHBoxLayout;
  header->setSpacing(20);
  tile_label = new QLabel(detail);
  tile_label->setFixedSize(HEADER_TILE, HEADER_TILE);
  header->addWidget(tile_label);

  auto *head_col = new QVBoxLayout;
  head_col->setSpacing(6);
  head_col->addStretch();
  eyebrow_label = new QLabel(detail);
  {
    auto f = theme::mono(9);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 2);
    eyebrow_label->setFont(f);
    eyebrow_label->setForegroundRole(QPalette::Highlight);
  }
  head_col->addWidget(eyebrow_label);

  title_label = new QLabel(detail);
  {
    auto f = theme::display_serif(34);
    f.setWeight(QFont::DemiBold);
    f.setLetterSpacing(QFont::AbsoluteSpacing, -0.6);
    title_label->setFont(f);
  }
  head_col->addWidget(title_label);

  meta_label = new QLabel(detail);
  meta_label->setFont(theme::mono(11));
  meta_label->setForegroundRole(QPalette::Dark);
  head_col->addWidget(meta_label);

  auto *actions = new QHBoxLayout;
  actions->setSpacing(10);
  play_button = theme::ui::primary_button("Play", detail);
  shuffle_button = theme::ui::outline_pill_button("Shuffle", detail);
  more_button = theme::ui::outline_pill_button("···", detail);
  actions->addWidget(play_button);
  actions->addWidget(shuffle_button);
  actions->addWidget(more_button);
  actions->addStretch();

  connect(more_button, &QPushButton::clicked, this, [this] {
    QMenu menu(this);
    auto *rename = menu.addAction("Rename…");
    auto *del = menu.addAction("Delete…");
    connect(rename, &QAction::triggered, this, &PlaylistsView::rename_current);
    connect(del, &QAction::triggered, this, &PlaylistsView::delete_current);
    menu.exec(more_button->mapToGlobal(QPoint(0, more_button->height())));
  });
  head_col->addSpacing(4);
  head_col->addLayout(actions);
  head_col->addStretch();

  header->addLayout(head_col, 1);
  dl->addLayout(header);

  dl->addWidget(theme::ui::section_header("ITEMS", detail));

  detail_stack = new QStackedWidget(detail);

  items = new PlaylistItemView(detail);
  items->setFrameShape(QFrame::NoFrame);
  items->setSelectionMode(QAbstractItemView::ExtendedSelection);
  items->setMouseTracking(true);
  items->setContextMenuPolicy(Qt::CustomContextMenu);
  {
    auto pal = items->palette();
    pal.setColor(QPalette::Base, Qt::transparent);
    items->setPalette(pal);
  }
  item_model = new PlaylistItemModel(this->libman.get(), this);
  item_model->set_favorites(this->favman.get());
  items->setModel(item_model);
  item_delegate = std::make_unique<PlaylistItemDelegate>(this->artman.get());
  items->setItemDelegate(item_delegate.get());
  detail_stack->addWidget(items);

  // Empty state (Favorited Tracks with nothing favorited).
  auto *empty = new QWidget(detail);
  auto *el = new QVBoxLayout(empty);
  el->addStretch();
  auto *heart = new QLabel("♥", empty);
  {
    auto f = theme::ui_sans(46);
    heart->setFont(f);
    heart->setAlignment(Qt::AlignCenter);
    heart->setForegroundRole(QPalette::Highlight);
  }
  el->addWidget(heart);
  auto *empty_title = new QLabel("No favorited tracks yet", empty);
  {
    auto f = theme::display_serif(18);
    f.setWeight(QFont::Medium);
    empty_title->setFont(f);
    empty_title->setAlignment(Qt::AlignCenter);
  }
  el->addWidget(empty_title);
  auto *empty_sub = new QLabel("Tap the heart on any track to add it here.", empty);
  {
    empty_sub->setFont(theme::mono(11));
    empty_sub->setAlignment(Qt::AlignCenter);
    empty_sub->setForegroundRole(QPalette::PlaceholderText);
  }
  el->addWidget(empty_sub);
  el->addStretch();
  detail_stack->addWidget(empty);

  dl->addWidget(detail_stack, 1);

  root->addWidget(detail, 1);

  // ── Wiring ──
  connect(sidebar->selectionModel(), &QItemSelectionModel::currentRowChanged,
      this, [this] { on_sidebar_selection(); });

  connect(play_button, &QPushButton::clicked, this, [this] {
    if (!current_songs.isEmpty())
      emit play_songs(0, current_songs);
  });
  connect(shuffle_button, &QPushButton::clicked, this, [this] {
    auto s = current_songs;
    std::shuffle(s.begin(), s.end(), std::mt19937{std::random_device{}()});
    if (!s.isEmpty())
      emit play_songs(0, s);
  });
  connect(items, &QListView::activated, this, [this](const QModelIndex &idx) {
    int flat = item_model->flat_index(idx.row());
    if (flat >= 0)
      emit play_songs(uint32_t(flat), current_songs);
  });
  connect(items, &QListView::customContextMenuRequested,
      this, &PlaylistsView::items_context_menu);
  connect(item_model, &PlaylistItemModel::rearrange_rows, this,
      [this](int target, const QList<int> &moved) {
        if (!current_auto && !current_id.isEmpty())
          this->plman->rearrange(current_id, target, moved);
      });

  if (this->favman)
    {
      connect(items, &PlaylistItemView::favorite_toggled,
          this->favman.get(), &FavoritesManager::toggle);
      // Optimistic, instant: pop the heart + update the count. The auto-list's
      // membership is refetched separately on the backend's *post-write*
      // favorite_changed (below), never here — fetching getStarred2 on the
      // optimistic signal races the Subsonic star/unstar write and reads
      // pre-write state (the reported flaky-add bug).
      connect(this->favman.get(), &FavoritesManager::favorite_changed, this,
          [this](const QString &uri, bool fav) {
            if (fav)
              item_delegate->pop(uri);
            sidebar_model->set_favorites_count(this->favman->all().size());
          });
      connect(this->favman.get(), &FavoritesManager::favorites_reset, this, [this] {
        sidebar_model->set_favorites_count(this->favman->all().size());
      });
      // The songs arrive from the same fetch as the uri set. Not gated on
      // visibility: the auto row stays selected, so load_detail never re-runs on
      // reopen and the model has to stay current while hidden.
      connect(this->favman.get(), &FavoritesManager::songs_changed, this, [this] {
        if (current_auto)
          refresh_items(this->favman->songs());
      });
      connect(item_delegate.get(), &PlaylistItemDelegate::needs_repaint, this,
          [this] { items->viewport()->update(); });
    }

  connect(this->plman.get(), &PlaylistsManager::playlists_reset, this, [this] {
    rebuild_sidebar();
  });
  connect(this->plman.get(), &PlaylistsManager::playlist_updated, this,
      [this](const QString &id) {
        sidebar->viewport()->update();
        if (!current_auto && id == current_id)
          {
            auto songs = this->plman->songs(id);
            if (songs)
              refresh_items(*songs);
            update_header();
          }
      });

  if (backend && this->favman)
    {
      // Re-read the auto list only once the backend confirms the star/unstar
      // write (Subsonic emits favorite_changed post-write), so the read sees
      // committed state; asking on the optimistic toggle reads pre-write state
      // (the reported flaky-add bug). Skipped while a stored playlist is
      // selected — nothing is displaying the auto list's contents then. MPD
      // emits no per-item signal and is covered by its idle-driven bulk reload.
      // Residual edge: rapid toggling can still reorder the responses (QNAM
      // doesn't order them) — acceptable for the single-toggle case.
      connect(backend, &Backend::favorite_changed, this, [this] {
        if (current_auto)
          this->favman->request_refresh();
      });
    }

  connect(this->artman.get(), &AlbumArtManager::art_ready, this, [this] {
    items->viewport()->update();
    sidebar->viewport()->update();
    render_header_tile();
  });
  // A thumbnail rebuild renders mosaics live but doesn't persist them (a
  // constituent cover may still be a placeholder mid-rebuild). Repaint once it
  // finishes so freshly-fetched playlists' mosaics bake + cache with final art.
  connect(this->artman.get(), &AlbumArtManager::thumbnail_updating, this,
      [this](bool updating) {
        if (!updating)
          sidebar->viewport()->update();
      });

  rebuild_sidebar();
}

PlaylistsView::~PlaylistsView() = default;

void
PlaylistsView::set_current_uri(const QString &uri)
{
  current_uri = uri;
  item_model->set_current_uri(uri);
}

auto
PlaylistsView::is_auto_selected() const -> bool
{
  auto idx = sidebar->currentIndex();
  return !idx.isValid() || idx.row() == 0;
}

void
PlaylistsView::rebuild_sidebar()
{
  // Keep the selection stable by id across a reset.
  QString want_id = current_id;
  bool want_auto = current_auto;

  sidebar_model->set_playlists(plman->playlists());
  if (favman)
    sidebar_model->set_favorites_count(favman->all().size());

  int n = plman->playlists().size();
  sidebar_header->setText(QString("PLAYLISTS ～ %1").arg(n));

  // Fetch contents only for playlists whose cover mosaic AlbumArtManager hasn't
  // already cached (persisted per server, validated by last-modified) — a warm
  // cache draws the sidebar with zero getPlaylist calls; only new/changed
  // playlists are fetched.
  const QSize tile{PLAYLIST_SIDEBAR_TILE, PLAYLIST_SIDEBAR_TILE};
  int fetched = 0;
  for (const auto &pl : plman->playlists())
    if (!plman->songs(pl.id)
        && !artman->has_playlist_mosaic(pl.id, pl.cache_stamp(), tile))
      {
        plman->ensure_songs(pl.id);
        ++fetched;
      }
  spdlog::debug("PlaylistsView: {} of {} playlists need a mosaic fetch", fetched, n);

  int row = 0;
  if (!want_auto)
    {
      int r = sidebar_model->row_for_id(want_id);
      // A rename that changed the id (MPD) drops want_id; re-land on the new name.
      if (r < 0 && !pending_rename_target.isEmpty())
        r = sidebar_model->row_for_id(pending_rename_target);
      row = r >= 0 ? r : 0; // deleted playlist → fall back to Favorited Tracks
    }
  pending_rename_target.clear();
  sidebar->setCurrentIndex(sidebar_model->index(row));
  on_sidebar_selection();
}

void
PlaylistsView::on_sidebar_selection()
{
  auto idx = sidebar->currentIndex();
  bool is_auto = !idx.isValid() || idx.row() == 0;
  current_auto = is_auto;
  current_id = is_auto ? QString{} : sidebar_model->playlist_id(idx.row());
  load_detail();
}

void
PlaylistsView::load_detail()
{
  update_header();

  // The auto Favorited Tracks list is server-ordered and read-only; only real
  // stored playlists can be drag-reordered.
  items->set_reorderable(!current_auto);

  if (current_auto)
    {
      displayed_id.clear();
      // Whatever the last fetch delivered — a blank list before the first one
      // answers, never the empty state, which would flash before the songs
      // arrive. refresh_items shows it only once the fetch has answered and the
      // list is genuinely empty.
      refresh_items(favman ? favman->songs() : QList<song>{});
      return;
    }

  plman->ensure_songs(current_id);
  auto cached = plman->songs(current_id);
  if (cached)
    {
      displayed_id = current_id;
      refresh_items(*cached);
    }
  else if (current_id != displayed_id)
    {
      // Navigating to a not-yet-loaded playlist: blank until its songs arrive.
      displayed_id = current_id;
      refresh_items({});
    }
  // else: the currently-shown playlist is being refetched (e.g. after our own
  // optimistic reorder cleared song_cache) — keep its rows and mosaic; the
  // pending playlist_updated repopulates and the set_data guard no-ops when the
  // refetched order matches the optimistic one.
}

void
PlaylistsView::refresh_items(const QList<song> &songs)
{
  current_songs = songs;
  item_model->set_data(songs);
  item_model->set_current_uri(current_uri);

  // Only show the "no favorited tracks" empty state once a fetch has actually
  // completed empty — never during the in-flight window (that was the flash).
  bool show_empty = current_auto && favman && favman->songs_loaded() && songs.isEmpty();
  detail_stack->setCurrentIndex(show_empty ? 1 : 0);

  bool has_songs = !songs.isEmpty();
  play_button->setEnabled(has_songs);
  shuffle_button->setEnabled(has_songs);

  render_header_tile();
}

void
PlaylistsView::update_header()
{
  // Editing (rename/delete) is meaningless for the auto Favorited Tracks list.
  more_button->setVisible(!current_auto);

  if (current_auto)
    {
      eyebrow_label->setText("AUTO PLAYLIST");
      title_label->setText("Favorited Tracks");
      int n = favman ? favman->all().size() : 0;
      meta_label->setText(QString("%1 %2  ～  updates automatically")
              .arg(n)
              .arg(n == 1 ? "track" : "tracks"));
    }
  else
    {
      eyebrow_label->setText("PLAYLIST");
      auto info = plman->info(current_id);
      title_label->setText(info ? info->name : current_id);

      QString meta;
      if (info)
        {
          meta = info->song_count < 0
                     ? QStringLiteral("— items")
                     : QString("%1 %2").arg(info->song_count).arg(info->song_count == 1 ? "item" : "items");
          if (info->duration_ms > 0)
            meta += QString("  ～  %1").arg(timeutil::ms_to_text(uint32_t(info->duration_ms)));
          auto rel = timeutil::relative_label(info->last_modified);
          if (!rel.isEmpty())
            meta += QString("  ～  updated %1").arg(rel);
        }
      meta_label->setText(meta);
    }
  render_header_tile();
}

void
PlaylistsView::render_header_tile()
{
  const QSize sz{HEADER_TILE, HEADER_TILE};

  if (!current_auto)
    {
      auto info = plman->info(current_id);
      auto pm = artman->get_playlist_mosaic(current_id,
          info ? info->cache_stamp() : QString{}, plman->mosaic_hashes(current_id), sz);
      if (!pm.isNull())
        {
          tile_label->setPixmap(pm);
          return;
        }
      // A refetch of the already-shown playlist (e.g. after an optimistic
      // reorder) briefly leaves its contents uncached, so no mosaic can be built
      // yet. Keep the existing tile rather than flashing the placeholder — the
      // real mosaic re-renders once the contents land.
      if (current_id == displayed_id)
        return;
    }

  qreal dpr = devicePixelRatioF();
  QPixmap pm(sz * dpr);
  pm.setDevicePixelRatio(dpr);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  QRect r(0, 0, HEADER_TILE, HEADER_TILE);
  if (current_auto)
    covermosaic::paint_heart(&p, r, palette().color(QPalette::Highlight),
        palette().color(QPalette::HighlightedText));
  else
    p.fillRect(r, palette().color(QPalette::Midlight)); // placeholder until contents load
  p.end();
  tile_label->setPixmap(pm);
}

auto
PlaylistsView::row_songs(const QModelIndex &index) const -> QList<song>
{
  int first = index.data(PlaylistItemModel::UserRoleFirstIndex).toInt();
  int span = index.data(PlaylistItemModel::UserRoleSpan).toInt();
  if (first < 0 || first >= current_songs.size())
    return {};
  return current_songs.mid(first, span);
}

void
PlaylistsView::items_context_menu(const QPoint &pos)
{
  auto idx = items->indexAt(pos);
  if (!idx.isValid())
    return;
  auto *sel = items->selectionModel();
  if (!sel->isSelected(idx))
    {
      sel->select(idx, QItemSelectionModel::ClearAndSelect);
      items->setCurrentIndex(idx);
    }

  auto selected = sel->selectedIndexes();
  std::sort(selected.begin(), selected.end(),
      [](const QModelIndex &a, const QModelIndex &b) { return a.row() < b.row(); });

  QList<song> songs;
  for (const auto &s : selected)
    songs += row_songs(s);
  if (songs.isEmpty())
    return;

  QMenu menu(this);
  auto *play = menu.addAction(QIcon::fromTheme("media-playback-start"), "Play");
  auto *next = menu.addAction(QIcon::fromTheme("media-skip-forward"), "Play Next");
  auto *append = menu.addAction(QIcon::fromTheme("list-add"), "Add to Queue");

  connect(play, &QAction::triggered, this, [this, songs] { emit play_songs(0, songs); });
  connect(next, &QAction::triggered, this, [this, songs] { emit play_songs_next(songs); });
  connect(append, &QAction::triggered, this, [this, songs] { emit queue_songs(songs); });

  // Editable stored playlists (not the auto Favorited Tracks list) can remove.
  if (!current_auto)
    {
      menu.addSeparator();
      auto *remove = menu.addAction(
          QIcon::fromTheme("list-remove"), "Remove from playlist");
      connect(remove, &QAction::triggered, this,
          [this, selected] { remove_rows(selected); });
    }

  menu.exec(items->viewport()->mapToGlobal(pos));
}

void
PlaylistsView::create_playlist()
{
  bool ok = false;
  auto name = QInputDialog::getText(this, "New Playlist", "Playlist name:",
      QLineEdit::Normal, QString{}, &ok)
                  .trimmed();
  if (!ok)
    return;
  if (!valid_playlist_name(name))
    {
      QMessageBox::warning(this, "New Playlist",
          "A playlist name can't be empty or contain '/' or a line break.");
      return;
    }
  // Empty stored playlists exist on Subsonic but not MPD (playlistadd needs a
  // uri; playlistclear won't create one) — on MPD this is a no-op until tracks
  // are added via the add-to-playlist dialog.
  plman->create(name, {});
}

void
PlaylistsView::rename_current()
{
  if (current_auto || current_id.isEmpty())
    return;
  auto info = plman->info(current_id);
  QString current_name = info ? info->name : current_id;
  bool ok = false;
  auto name = QInputDialog::getText(this, "Rename Playlist", "Playlist name:",
      QLineEdit::Normal, current_name, &ok)
                  .trimmed();
  if (!ok || name == current_name)
    return;
  if (!valid_playlist_name(name))
    {
      QMessageBox::warning(this, "Rename Playlist",
          "A playlist name can't be empty or contain '/' or a line break.");
      return;
    }
  pending_rename_target = name; // re-land selection when the refetch lands
  plman->rename(current_id, name);
}

void
PlaylistsView::delete_current()
{
  if (current_auto || current_id.isEmpty())
    return;
  auto info = plman->info(current_id);
  QString name = info ? info->name : current_id;
  auto reply = QMessageBox::question(this, "Delete Playlist",
      QString("Delete the playlist \"%1\"? This can't be undone.").arg(name),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (reply == QMessageBox::Yes)
    plman->remove(current_id); // refetch drops selection back to Favorited Tracks
}

void
PlaylistsView::remove_rows(const QModelIndexList &rows)
{
  if (current_auto || current_id.isEmpty())
    return;
  // Coalesced album rows cover a span of flat positions; expand each selected
  // display row into its flat indexes. remove_at sorts descending internally.
  QList<int> positions;
  for (const auto &idx : rows)
    {
      int first = idx.data(PlaylistItemModel::UserRoleFirstIndex).toInt();
      int span = idx.data(PlaylistItemModel::UserRoleSpan).toInt();
      for (int i = 0; i < span; ++i)
        positions.push_back(first + i);
    }
  if (!positions.isEmpty())
    plman->remove_at(current_id, positions);
}

void
PlaylistsView::keyPressEvent(QKeyEvent *event)
{
  if (event->key() == Qt::Key_Escape)
    {
      emit close_requested();
      return;
    }
  QWidget::keyPressEvent(event);
}
