#include "albumview.hh"
#include "ui_albumview.h"
#include "theme.hh"
#include "ui/theme/components.hh"
#include "ui/songlistview.hh"
#include "controller/favoritesmanager.hh"
#include "timeutil.hh"

#include <QMap>
#include <QList>
#include <QEvent>
#include <QResizeEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QLinearGradient>
#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <algorithm>
#include <random>

AlbumView::AlbumView(std::shared_ptr<AlbumArtManager> artman,
    std::shared_ptr<FavoritesManager> favman, QWidget *parent)
  : QWidget(parent)
  , ui(std::make_unique<Ui::AlbumView>())
  , artman{artman}
  , favman{std::move(favman)}
{
  ui->setupUi(this);

  // Minimum overall dimensions: 28 + 300 (info col) + 32 + ~170 (tracklist) + 28
  setMinimumWidth(560);

  // Back button: shared back-button look, top-left
  connect(ui->backbutton, &QPushButton::clicked, [&] { emit close(); });
  theme::ui::style_as_back_button(ui->backbutton, "Library");

  // Cover: thin border, drop shadow
  ui->albumcover->setStyleSheet(
      "border: 1px solid palette(midlight);");
  theme::ui::cover_shadow(ui->albumcover, 260);

  // Action buttons: shared component recipes (filled play, outlined shuffle/queue)
  theme::ui::style_as_primary_button(ui->playbutton);
  theme::ui::style_as_outline_button(ui->shufflebutton);
  theme::ui::style_as_outline_button(ui->queuebutton);
  theme::ui::style_as_outline_button(ui->addplaylistbutton);
  ui->addplaylistbutton->setVisible(false); // shown when the backend supports playlists

  song_model = new SongListModel(this);
  song_model->set_favorites(this->favman.get());
  ui->songlist->setModel(song_model);
  ui->songlist->setDragDropMode(QAbstractItemView::DragOnly);
  ui->songlist->setSelectionMode(QAbstractItemView::ExtendedSelection);
  ui->songlist->setMouseTracking(true);
  ui->songlist->viewport()->setMouseTracking(true);

  song_item = std::make_unique<SongListItem>();
  ui->songlist->setItemDelegate(song_item.get());

  // Heart hit-test → toggle favorite. The pop bump runs on confirmation (the
  // optimistic favorite_changed) so it fires for external stars too; repaints
  // are driven by the delegate's animation.
  if (this->favman)
    {
      connect(ui->songlist, &SongListView::favorite_toggled,
          this->favman.get(), &FavoritesManager::toggle);
      connect(this->favman.get(), &FavoritesManager::favorite_changed, this,
          [this](const QString &uri, bool fav) {
            if (fav)
              song_item->pop(uri);
          });
      connect(song_item.get(), &SongListItem::needs_repaint, this,
          [this] { ui->songlist->viewport()->update(); });
    }

  connect(ui->songlist, &SongListView::add_to_playlist_requested,
      this, [this](const song &s) { emit add_song_to_playlist(s); });

  // Transparent songlist so the header wash shows through behind the top rows.
  // Done via palette, NOT a stylesheet: any stylesheet on a QAbstractItemView
  // routes item painting through QStyleSheetStyle, which stops setting
  // State_MouseOver on the delegate's option — killing the row hover fill.
  ui->songlist->viewport()->setAutoFillBackground(false);
  auto songlist_pal = ui->songlist->palette();
  songlist_pal.setColor(QPalette::Base, Qt::transparent);
  ui->songlist->setPalette(songlist_pal);

  // Tracklist header: static hairline rule
  ui->tracklist_rule->setBackgroundRole(QPalette::Midlight);
  ui->tracklist_rule->setAutoFillBackground(true);

  update_fonts(); // app-font changes are handled in changeEvent()

  connect(ui->songlist, &QListView::activated,
      this, &AlbumView::songlist_activated);
  ui->songlist->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(ui->songlist, &QListView::customContextMenuRequested,
      this, &AlbumView::songlist_context_menu);
  connect(ui->playbutton, &QPushButton::clicked,
      [&] { emit activated(0, song_model->get_data()); });
  connect(ui->shufflebutton, &QPushButton::clicked, [&] {
    auto songs = song_model->get_data();
    std::shuffle(songs.begin(), songs.end(), std::mt19937{std::random_device{}()});
    emit activated(0, songs);
  });
  connect(ui->queuebutton, &QPushButton::clicked,
      [&] { emit queue_songs(song_model->get_data()); });
  connect(ui->addplaylistbutton, &QPushButton::clicked,
      [&] { emit add_album_to_playlist(selected_album); });
}

auto
AlbumView::set_playlists_available(bool available) -> void
{
  playlists_available = available;
  ui->addplaylistbutton->setVisible(available);
  song_item->set_playlists_available(available);
}

AlbumView::~AlbumView() = default;

auto
AlbumView::resizeEvent(QResizeEvent *event) -> void
{
  QWidget::resizeEvent(event);
}

auto
AlbumView::paintEvent(QPaintEvent *event) -> void
{
  QColor accent = artman->get_accent(selected_album.album_hash);
  if (!accent.isValid())
    accent = palette().highlight().color();
  accent.setAlphaF(0.22);
  QColor transparent = accent;
  transparent.setAlpha(0);
  QLinearGradient g(0, 0, 0, 360);
  g.setColorAt(0.0, accent);
  g.setColorAt(1.0, transparent);
  QPainter p(this);
  p.fillRect(QRect(0, 0, width(), 360), g);
  QWidget::paintEvent(event);
}

auto
AlbumView::changeEvent(QEvent *event) -> void
{
  QWidget::changeEvent(event);
  if (event->type() == QEvent::ApplicationFontChange)
    {
      update_fonts();
      ui->songlist->reset();
    }
}

auto
AlbumView::update_fonts() -> void
{
  // "ALBUM" kind label: mono 9px, accent color, 2px tracking
  auto kf = theme::mono(9);
  kf.setLetterSpacing(QFont::AbsoluteSpacing, 2);
  ui->kind_label->setFont(kf);
  ui->kind_label->setForegroundRole(QPalette::Highlight);

  // Title: IBM Plex Sans display, 26px SemiBold (--title-weight: 600), tight tracking
  auto tf = theme::display_serif(26);
  tf.setWeight(QFont::DemiBold);
  tf.setLetterSpacing(QFont::AbsoluteSpacing, -0.6); // -0.022em at 26px
  ui->titlelabel->setFont(tf);

  // Artist: sans, 14px weight 500
  auto af = theme::ui_sans(14);
  af.setWeight(QFont::Medium);
  ui->artistlabel->setFont(af);

  // Meta row: mono, dim color
  ui->meta_label->setFont(theme::mono(11));
  ui->meta_label->setForegroundRole(QPalette::Dark);

  // Tracklist header row: "TRACKLIST ── N tracks" (mono 9 Medium, dim2, 2px tracking)
  auto hf = theme::type::section_label();
  hf.setLetterSpacing(QFont::AbsoluteSpacing, 2);
  ui->tracklist_label->setFont(hf);
  ui->tracklist_label->setForegroundRole(QPalette::PlaceholderText);
  ui->tracklist_count->setFont(theme::mono(10));
  ui->tracklist_count->setForegroundRole(QPalette::PlaceholderText);
}

auto
AlbumView::songlist_activated(const QModelIndex &index) -> void
{
  emit activated(index.row(), song_model->get_data());
}

auto
AlbumView::songlist_context_menu(const QPoint &pos) -> void
{
  auto idx = ui->songlist->indexAt(pos);
  if (!idx.isValid())
    return;

  // Right-clicking a row outside the current selection retargets to just that
  // row, matching the album-grid menu behavior.
  auto *sel = ui->songlist->selectionModel();
  if (!sel->isSelected(idx))
    {
      sel->select(idx, QItemSelectionModel::ClearAndSelect);
      ui->songlist->setCurrentIndex(idx);
    }

  auto selected = sel->selectedIndexes();
  std::sort(selected.begin(), selected.end(),
      [](const QModelIndex &a, const QModelIndex &b) { return a.row() < b.row(); });

  const auto &all = song_model->get_data();
  QList<song> songs;
  for (const auto &s : selected)
    if (s.row() >= 0 && s.row() < all.size())
      songs.push_back(all[s.row()]);
  if (songs.isEmpty())
    return;

  QMenu menu(this);
  auto *play = menu.addAction(QIcon::fromTheme("media-playback-start"), "Play");
  auto *next = menu.addAction(QIcon::fromTheme("media-skip-forward"), "Play Next");
  auto *append = menu.addAction(QIcon::fromTheme("list-add"), "Add to Queue");

  connect(play, &QAction::triggered, this, [this, songs] { emit activated(0, songs); });
  connect(next, &QAction::triggered, this, [this, songs] { emit play_songs_next(songs); });
  connect(append, &QAction::triggered, this, [this, songs] { emit queue_songs(songs); });

  if (playlists_available)
    {
      menu.addSeparator();
      auto *add = menu.addAction(QIcon::fromTheme("list-add"), "Add to playlist…");
      auto primary = songs.first();
      connect(add, &QAction::triggered, this, [this, primary] { emit add_song_to_playlist(primary); });
    }

  menu.exec(ui->songlist->viewport()->mapToGlobal(pos));
}

auto
AlbumView::set_data(album a, const QList<song> &s) -> void
{
  selected_album = a;

  auto pixmap = artman->get_art(a.album_hash, ui->albumcover->size());
  ui->albumcover->setPixmap(pixmap);

  ui->titlelabel->setText(a.name);
  ui->artistlabel->setText(a.artist);

  // Tracklist not loaded yet: show placeholder rows + known count (from the
  // album metadata) so the page renders complete and doesn't reflow when the
  // real tracks arrive.
  if (s.isEmpty() && a.song_count > 0)
    {
      auto count = QString("%1 %2").arg(a.song_count).arg(a.song_count == 1 ? "track" : "tracks");
      auto year = timeutil::year_label(a.date);
      ui->meta_label->setText(year.isEmpty() ? count
                                             : QString("%1  ～  %2").arg(year, count));
      ui->tracklist_count->setText(count);
      ui->tracklist_label->setVisible(true);
      ui->tracklist_rule->setVisible(true);
      ui->tracklist_count->setVisible(true);
      song_model->set_skeleton(a.song_count);
      update();
      return;
    }

  // Compute total duration and disc count
  uint32_t total_ms = 0;
  for (const auto &song : s)
    total_ms += song.duration;
  auto n = s.size();

  uint32_t max_disc = 0;
  for (const auto &song : s)
    max_disc = std::max(max_disc, song.disc_number);
  bool is_multi_disc = max_disc > 1;

  // Build meta string: [year ～] N tracks [～ N discs] ～ duration
  auto year = timeutil::year_label(a.date);
  auto meta = year.isEmpty()
                  ? QString("%1 %2").arg(n).arg(n == 1 ? "track" : "tracks")
                  : QString("%1  ～  %2 %3")
                        .arg(year)
                        .arg(n)
                        .arg(n == 1 ? "track" : "tracks");
  if (is_multi_disc)
    meta += QString("  ～  %1 discs").arg(max_disc);
  meta += QString("  ～  %1").arg(timeutil::ms_to_text(total_ms));
  ui->meta_label->setText(meta);

  // Tracklist count: "N track(s)"
  ui->tracklist_count->setText(
      QString("%1 %2").arg(n).arg(n == 1 ? "track" : "tracks"));

  // Hide the TRACKLIST section header for multi-disc albums; per-disc headers
  // are rendered inline by the delegate instead.
  ui->tracklist_label->setVisible(!is_multi_disc);
  ui->tracklist_rule->setVisible(!is_multi_disc);
  ui->tracklist_count->setVisible(!is_multi_disc);

  // Show per-track artist in the delegate only for various-artists albums
  bool is_va = std::ranges::any_of(s, [&](const song &song) {
    return !song.artist.isEmpty() && song.artist != a.artist;
  });
  song_model->set_show_artist(is_va);
  song_model->set_data(s);

  update();
}

auto
AlbumView::set_current_song(const song &song) -> void
{
  song_model->set_current_uri(song.uri);
}
