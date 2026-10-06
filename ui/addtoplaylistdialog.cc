#include "addtoplaylistdialog.hh"

#include <functional>

#include <QEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QVBoxLayout>

#include "controller/albumartmanager.hh"
#include "controller/backend.hh"
#include "controller/playlistsmanager.hh"
#include "theme.hh"
#include "ui/theme/components.hh"
#include "timeutil.hh"

namespace {
constexpr int DIALOG_W = 460;
constexpr int TILE = 32;

// "12 items ～ updated 3 days ago" — either half omitted when unknown.
auto
meta_text(const playlist_info &info) -> QString
{
  QStringList parts;
  if (info.song_count >= 0)
    parts << QString("%1 %2").arg(info.song_count).arg(info.song_count == 1 ? "item" : "items");
  else
    parts << QStringLiteral("— items");
  auto rel = timeutil::relative_label(info.last_modified);
  if (!rel.isEmpty())
    parts << QString("updated %1").arg(rel);
  return parts.join(QStringLiteral(" ～ "));
}
} // namespace

// One playlist entry: mosaic tile, name/meta, and a right-aligned add/added
// state chip. The whole row is clickable; hover and membership drive its fill.
class AddToPlaylistDialog::Row : public QWidget {
public:
  Row(const QString &pl_id, QWidget *parent)
    : QWidget(parent)
    , id(pl_id)
  {
    setCursor(Qt::PointingHandCursor);
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(16, 10, 16, 10);
    lay->setSpacing(11);

    tile = new QLabel(this);
    tile->setFixedSize(TILE, TILE);
    lay->addWidget(tile);

    auto *text_col = new QVBoxLayout;
    text_col->setContentsMargins(0, 0, 0, 0);
    text_col->setSpacing(1);
    name = new QLabel(this);
    auto nf = theme::ui_sans(13);
    nf.setWeight(QFont::Medium);
    name->setFont(nf);
    meta = new QLabel(this);
    meta->setFont(theme::type::mono_meta());
    meta->setForegroundRole(QPalette::PlaceholderText);
    text_col->addWidget(name);
    text_col->addWidget(meta);
    lay->addLayout(text_col, 1);

    state = new QLabel(this);
    auto sf = theme::mono(9);
    sf.setWeight(QFont::Medium);
    sf.setLetterSpacing(QFont::AbsoluteSpacing, 1);
    state->setFont(sf);
    lay->addWidget(state);
  }

  void set_click(std::function<void()> cb) { on_click = std::move(cb); }

  void set_state(bool is_member, bool is_loading)
  {
    member = is_member;
    loading = is_loading;
    if (loading)
      {
        state->setText(QStringLiteral("…"));
        state->setForegroundRole(QPalette::PlaceholderText);
      }
    else if (member)
      {
        state->setText(QStringLiteral("✓ ADDED"));
        state->setForegroundRole(QPalette::Highlight);
      }
    else
      {
        state->setText(QStringLiteral("+ ADD"));
        state->setForegroundRole(QPalette::Dark);
      }
    update();
  }

  QString id;
  QLabel *tile;
  QLabel *name;
  QLabel *meta;
  QLabel *state;
  bool member = false;
  bool loading = true;

protected:
  void enterEvent(QEnterEvent *) override
  {
    hovered = true;
    update();
  }
  void leaveEvent(QEvent *) override
  {
    hovered = false;
    update();
  }
  void mousePressEvent(QMouseEvent *event) override
  {
    if (event->button() == Qt::LeftButton && on_click)
      on_click();
  }
  void paintEvent(QPaintEvent *) override
  {
    QPainter p(this);
    if (member)
      {
        QColor tint = palette().color(QPalette::Highlight);
        tint.setAlpha(hovered ? 48 : 32);
        p.fillRect(rect(), tint);
      }
    else if (hovered)
      {
        p.fillRect(rect(), palette().color(QPalette::Button));
      }
  }

private:
  bool hovered = false;
  std::function<void()> on_click;
};

AddToPlaylistDialog::AddToPlaylistDialog(std::shared_ptr<PlaylistsManager> plman,
    std::shared_ptr<AlbumArtManager> artman, Backend *backend,
    playlist_subject subject, QWidget *parent)
  : QDialog(parent)
  , plman{std::move(plman)}
  , artman{std::move(artman)}
  , backend{backend}
  , subject{std::move(subject)}
{
  setWindowTitle(QStringLiteral("Add to Playlist"));
  setModal(true);
  setAttribute(Qt::WA_DeleteOnClose);
  setFixedWidth(DIALOG_W);

  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  // ── Header: eyebrow + close, then cover + subject title/meta ──────────────
  auto *header = new QWidget(this);
  auto *hlay = new QVBoxLayout(header);
  hlay->setContentsMargins(18, 14, 18, 16);
  hlay->setSpacing(9);

  auto *top_row = new QHBoxLayout;
  top_row->setContentsMargins(0, 0, 0, 0);
  auto *eyebrow = new QLabel(QStringLiteral("ADD TO PLAYLIST"), header);
  auto ef = theme::mono(9);
  ef.setLetterSpacing(QFont::AbsoluteSpacing, 2);
  eyebrow->setFont(ef);
  eyebrow->setForegroundRole(QPalette::Highlight);
  auto *close_btn = new QPushButton(QStringLiteral("✕"), header);
  close_btn->setFlat(true);
  close_btn->setCursor(Qt::PointingHandCursor);
  close_btn->setFixedSize(22, 22);
  close_btn->setForegroundRole(QPalette::PlaceholderText);
  connect(close_btn, &QPushButton::clicked, this, &QDialog::reject);
  top_row->addWidget(eyebrow);
  top_row->addStretch(1);
  top_row->addWidget(close_btn);
  hlay->addLayout(top_row);

  auto is_track = this->subject.kind == playlist_subject::Kind::Track;

  auto *subj_row = new QHBoxLayout;
  subj_row->setContentsMargins(0, 0, 0, 0);
  subj_row->setSpacing(11);
  auto *cover = new QLabel(header);
  cover->setFixedSize(38, 38);
  // A track subject carries a default-constructed album, so its cover key lives
  // on the track, not subject.alb.
  auto cover_hash = is_track ? this->subject.trk.album_hash
                             : this->subject.alb.album_hash;
  cover->setPixmap(this->artman->get_art(cover_hash, QSize(38, 38)));
  cover->setScaledContents(true);
  subj_row->addWidget(cover);

  auto *subj_col = new QVBoxLayout;
  subj_col->setContentsMargins(0, 0, 0, 0);
  subj_col->setSpacing(1);
  auto *subj_title = new QLabel(
      is_track ? this->subject.trk.title : this->subject.alb.name, header);
  auto stf = theme::ui_sans(15);
  stf.setWeight(QFont::DemiBold);
  subj_title->setFont(stf);
  auto *subj_meta = new QLabel(
      is_track ? this->subject.trk.artist : this->subject.alb.artist, header);
  subj_meta->setFont(theme::ui_sans(11));
  subj_meta->setForegroundRole(QPalette::Dark);
  for (auto *l : {subj_title, subj_meta})
    {
      l->setTextInteractionFlags(Qt::NoTextInteraction);
      QFontMetrics fm{l->font()};
      l->setText(fm.elidedText(l->text(), Qt::ElideRight, DIALOG_W - 38 - 11 - 36));
    }
  subj_col->addWidget(subj_title);
  subj_col->addWidget(subj_meta);
  subj_row->addLayout(subj_col, 1);
  hlay->addLayout(subj_row);
  root->addWidget(header);
  root->addWidget(theme::ui::separator(Qt::Horizontal, this));

  // ── Search ────────────────────────────────────────────────────────────────
  auto *search_wrap = new QWidget(this);
  auto *swl = new QVBoxLayout(search_wrap);
  swl->setContentsMargins(14, 10, 14, 10);
  search = new QLineEdit(search_wrap);
  search->setPlaceholderText(QStringLiteral("Find or create a playlist…"));
  search->setFont(theme::ui_sans(12));
  swl->addWidget(search);
  connect(search, &QLineEdit::textChanged, this, &AddToPlaylistDialog::apply_filter);
  connect(search, &QLineEdit::returnPressed, this, [this] {
    if (new_row->isVisible())
      create_from_term();
  });
  root->addWidget(search_wrap);
  root->addWidget(theme::ui::separator(Qt::Horizontal, this));

  // ── New-playlist action row ────────────────────────────────────────────────
  new_row = new QWidget(this);
  new_row->setCursor(Qt::PointingHandCursor);
  auto *nrl = new QHBoxLayout(new_row);
  nrl->setContentsMargins(16, 11, 16, 11);
  nrl->setSpacing(11);
  auto *plus = new QLabel(QStringLiteral("+"), new_row);
  plus->setFixedSize(34, 34);
  plus->setAlignment(Qt::AlignCenter);
  auto pf = theme::ui_sans(18);
  pf.setWeight(QFont::Medium);
  plus->setFont(pf);
  plus->setForegroundRole(QPalette::Highlight);
  {
    QColor tint = palette().color(QPalette::Highlight);
    tint.setAlpha(32);
    auto pal = plus->palette();
    pal.setColor(QPalette::Window, tint);
    plus->setPalette(pal);
    plus->setAutoFillBackground(true);
  }
  nrl->addWidget(plus);
  auto *ncol = new QVBoxLayout;
  ncol->setContentsMargins(0, 0, 0, 0);
  ncol->setSpacing(1);
  new_label = new QLabel(QStringLiteral("New playlist…"), new_row);
  auto nlf = theme::ui_sans(13);
  nlf.setWeight(QFont::Medium);
  new_label->setFont(nlf);
  new_label->setForegroundRole(QPalette::Highlight);
  auto *nsub = new QLabel(
      is_track ? QStringLiteral("Start with this track")
               : QStringLiteral("Start with this album"),
      new_row);
  nsub->setFont(theme::ui_sans(11));
  nsub->setForegroundRole(QPalette::Dark);
  ncol->addWidget(new_label);
  ncol->addWidget(nsub);
  nrl->addLayout(ncol, 1);
  new_row->installEventFilter(this);
  root->addWidget(new_row);
  root->addWidget(theme::ui::separator(Qt::Horizontal, this));

  // ── Scrollable playlist list ────────────────────────────────────────────────
  auto *scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setMinimumHeight(120);
  scroll->setMaximumHeight(300);
  list_container = new QWidget(scroll);
  list_layout = new QVBoxLayout(list_container);
  list_layout->setContentsMargins(0, 0, 0, 0);
  list_layout->setSpacing(0);
  // Empty-state message, centered in the list viewport between two spacers (rows
  // insert above the top one, so a populated list is unaffected).
  list_layout->addStretch(1);
  empty_label = new QLabel(list_container);
  empty_label->setFont(theme::ui_sans(12));
  empty_label->setForegroundRole(QPalette::PlaceholderText);
  empty_label->setAlignment(Qt::AlignCenter);
  empty_label->setWordWrap(true);
  empty_label->setContentsMargins(24, 0, 24, 0);
  empty_label->setVisible(false);
  list_layout->addWidget(empty_label);
  list_layout->addStretch(1);
  scroll->setWidget(list_container);
  root->addWidget(scroll, 1);
  root->addWidget(theme::ui::separator(Qt::Horizontal, this));

  // ── Footer ──────────────────────────────────────────────────────────────────
  auto *footer = new QWidget(this);
  auto *fl = new QHBoxLayout(footer);
  fl->setContentsMargins(14, 10, 14, 10);
  fl->setSpacing(12);
  footer_count = new QLabel(footer);
  footer_count->setFont(theme::type::mono_meta());
  footer_count->setForegroundRole(QPalette::PlaceholderText);
  fl->addWidget(footer_count);
  fl->addStretch(1);
  auto *cancel = theme::ui::outline_pill_button(QStringLiteral("Cancel"), footer);
  auto *done = theme::ui::primary_button(QStringLiteral("Done"), footer);
  connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
  connect(done, &QPushButton::clicked, this, &QDialog::accept);
  fl->addWidget(cancel);
  fl->addWidget(done);
  root->addWidget(footer);

  // ── Data wiring ───────────────────────────────────────────────────────────
  // A reset drops the manager's contents cache; rebuild the rows (preserving
  // known membership) then re-request contents so membership resolves again.
  connect(this->plman.get(), &PlaylistsManager::playlists_reset,
      this, [this] {
        rebuild_rows();
        reload_contents();
        update_footer();
      });
  connect(this->plman.get(), &PlaylistsManager::playlist_updated,
      this, [this](const QString &id) {
        refresh_row(id);
        refresh_mosaic(id); // contents just landed → mosaic hashes now available
        update_footer();
      });
  connect(this->artman.get(), &AlbumArtManager::art_ready,
      this, [this](const QString &) { refresh_mosaics(); });
  // A disconnect or profile switch invalidates the subject (server-native ids)
  // and clears the playlist list — close rather than linger on stale state.
  connect(backend, &Backend::connection_update, this, [this](bool connected) {
    if (!connected)
      reject();
  });
  // Creating a playlist from this dialog is itself what can prove the server
  // has no playlist support, so the refusal lands while the dialog is open —
  // every remaining action in it would fail the same way.
  connect(backend, &Backend::capabilities_changed, this, [this, backend] {
    if (!backend->supports(Backend::Feature::Playlists))
      reject();
  });

  // Resolve subject songs. Track: self-contained. Album: fetch asynchronously,
  // keeping rows in the loading state until they arrive.
  if (is_track)
    {
      subject_songs = {this->subject.trk};
      resolved = true;
    }
  else
    {
      backend->fetch_songs(this->subject.alb, [this](const QList<song> &songs) {
        subject_songs = songs;
        resolved = true;
        // Subject now known — recompute membership from already-loaded contents.
        for (const auto &pl : this->plman->playlists())
          refresh_row(pl.id);
        update_footer();
      });
    }

  rebuild_rows();
  reload_contents();
  update_footer();
}

AddToPlaylistDialog::~AddToPlaylistDialog() = default;

auto
AddToPlaylistDialog::eventFilter(QObject *obj, QEvent *event) -> bool
{
  if (obj == new_row && event->type() == QEvent::MouseButtonPress)
    {
      auto *me = static_cast<QMouseEvent *>(event);
      if (me->button() == Qt::LeftButton)
        {
          create_from_term();
          return true;
        }
    }
  return QDialog::eventFilter(obj, event);
}

auto
AddToPlaylistDialog::membership_positions(const QList<song> &playlist) const -> QList<int>
{
  QSet<QString> want;
  for (const auto &s : subject_songs)
    want.insert(s.uri);
  QList<int> pos;
  for (int i = 0; i < playlist.size(); ++i)
    if (want.contains(playlist[i].uri))
      pos.push_back(i);
  return pos;
}

auto
AddToPlaylistDialog::is_member(const QList<song> &playlist) const -> bool
{
  if (subject_songs.isEmpty())
    return false;
  QSet<QString> have;
  for (const auto &s : playlist)
    have.insert(s.uri);
  for (const auto &s : subject_songs)
    if (!have.contains(s.uri))
      return false; // every subject track must be present (album: all of them)
  return true;
}

auto
AddToPlaylistDialog::rebuild_rows() -> void
{
  // Delete immediately (not deleteLater) so old rows leave the layout before we
  // insert the new ones — otherwise the deferred deletes would briefly stack
  // duplicates. Safe: rebuild is only ever driven by manager signals, never
  // from within a Row's own event handler.
  qDeleteAll(rows);
  rows.clear();

  int insert_at = 0; // rows sit at the top, above the empty-state spacers
  for (const auto &pl : plman->playlists())
    {
      auto *row = new Row(pl.id, list_container);
      row->name->setText(pl.name);
      row->meta->setText(meta_text(pl));
      row->set_click([this, id = pl.id] { toggle(id); });
      rows.insert(pl.id, row);
      list_layout->insertWidget(insert_at++, row);
      refresh_row(pl.id);
    }
  refresh_mosaics();
  apply_filter();
}

auto
AddToPlaylistDialog::reload_contents() -> void
{
  for (const auto &pl : plman->playlists())
    plman->ensure_songs(pl.id);
}

auto
AddToPlaylistDialog::refresh_row(const QString &id) -> void
{
  auto *row = rows.value(id);
  if (!row)
    return;
  if (auto info = plman->info(id))
    row->meta->setText(meta_text(*info));
  auto songs = plman->songs(id);
  if (songs.has_value() && subject_ready())
    {
      bool m = is_member(*songs);
      known_member[id] = m;
      row->set_state(m, false);
    }
  else if (known_member.contains(id))
    {
      // Contents momentarily dropped by a refetch — hold the last known state
      // rather than flickering to loading, then to the same value again.
      row->set_state(known_member.value(id), false);
    }
  else
    {
      row->set_state(false, true); // never resolved yet — genuinely loading
    }
}

auto
AddToPlaylistDialog::refresh_mosaic(const QString &id) -> void
{
  auto *row = rows.value(id);
  if (!row)
    return;
  auto info = plman->info(id);
  auto pm = artman->get_playlist_mosaic(id, info ? info->cache_stamp() : QString{},
      plman->mosaic_hashes(id), QSize(TILE, TILE));
  if (pm.isNull())
    {
      // Placeholder tile until contents/covers land (art_ready / playlist_updated
      // re-render).
      const qreal dpr = devicePixelRatioF();
      pm = QPixmap(QSize(TILE, TILE) * dpr);
      pm.setDevicePixelRatio(dpr);
      pm.fill(row->palette().color(QPalette::Button));
    }
  row->tile->setPixmap(pm);
}

auto
AddToPlaylistDialog::refresh_mosaics() -> void
{
  for (auto it = rows.cbegin(); it != rows.cend(); ++it)
    refresh_mosaic(it.key());
}

auto
AddToPlaylistDialog::apply_filter() -> void
{
  auto term = search->text().trimmed();
  bool any = false;
  bool exact = false;
  for (auto it = rows.cbegin(); it != rows.cend(); ++it)
    {
      auto name = it.value()->name->text();
      bool match = term.isEmpty() || name.contains(term, Qt::CaseInsensitive);
      it.value()->setVisible(match);
      any = any || match;
      if (name.compare(term, Qt::CaseInsensitive) == 0)
        exact = true;
    }

  // New-playlist row: "New playlist…" with an empty box, "Create "term"" for a
  // non-empty term that doesn't exactly match an existing name.
  bool offer_create = term.isEmpty() || !exact;
  new_row->setVisible(offer_create);
  new_label->setText(term.isEmpty() || exact
                         ? QStringLiteral("New playlist…")
                         : QStringLiteral("Create “%1”").arg(term));

  empty_label->setVisible(!any && !term.isEmpty());
  if (!any && !term.isEmpty())
    empty_label->setText(QStringLiteral("No playlist matches “%1”.").arg(term));
}

auto
AddToPlaylistDialog::toggle(const QString &id) -> void
{
  if (!subject_ready())
    return;
  auto songs = plman->songs(id);
  if (!songs.has_value())
    return; // contents unknown — can't compute a safe toggle yet
  if (is_member(*songs))
    {
      auto pos = membership_positions(*songs);
      if (!pos.isEmpty())
        plman->remove_at(id, pos);
    }
  else
    {
      // Add only the tracks not already present, so a partially-present album
      // (is_member is all-or-nothing) doesn't duplicate its existing tracks.
      QSet<QString> have;
      for (const auto &s : *songs)
        have.insert(s.uri);
      QList<song> missing;
      for (const auto &s : subject_songs)
        if (!have.contains(s.uri))
          missing.push_back(s);
      if (!missing.isEmpty())
        plman->add_songs(id, missing);
    }
  // The optimistic edit lands via playlist_updated; refresh now for instant feel.
  refresh_row(id);
  update_footer();
}

auto
AddToPlaylistDialog::create_from_term() -> void
{
  if (!subject_ready())
    return;
  auto name = search->text().trimmed();
  if (name.isEmpty())
    name = QStringLiteral("New Playlist"); // MPD ids can't be empty; Subsonic rejects it
  plman->create(name, subject_songs);
  search->clear();
}

auto
AddToPlaylistDialog::update_footer() -> void
{
  int count = 0;
  for (const auto &pl : plman->playlists())
    if (known_member.value(pl.id, false))
      ++count;
  footer_count->setText(count > 0
                            ? QString("In %1 playlist%2").arg(count).arg(count == 1 ? "" : "s")
                            : QStringLiteral("Not in any playlist"));
}
