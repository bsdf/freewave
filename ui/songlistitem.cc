#include "songlistitem.hh"
#include "songlistmodel.hh"
#include "timeutil.hh"
#include "theme.hh"

#include <QFont>
#include <QTime>
#include <QPixmap>
#include <QPainter>
#include <QFileInfo>
#include <QApplication>
#include <QFontMetrics>
#include <QAbstractItemModel>

#include <algorithm>

// Row geometry (matches design: 7px top/bottom padding)
static constexpr int ROW_PAD_V = 7;
static constexpr int ROW_PAD_H = 10;
static constexpr int NUM_COL_W = 20;   // track number column width
static constexpr int COL_GAP = 14;     // gap between number and title
static constexpr int DISC_HEIGHT = 28; // height of disc separator row
static constexpr int HEART_COL_W = 18; // favorite heart column (left of duration)
static constexpr int ADD_COL_W = 18;   // add-to-playlist column (left of heart)

SongListItem::SongListItem(QObject *parent)
  : QAbstractItemDelegate(parent)
{
  set_options({
      QSize(40, 40),
      QMargins(ROW_PAD_H, ROW_PAD_V, ROW_PAD_H, ROW_PAD_V),
  });
  tfont_bold = theme::type::body(true);
  tfont_normal = theme::type::body(false);
  afont = theme::ui_sans(11);
  mono_font = theme::type::mono_meta();

  connect(&hearts, &HeartPopAnimator::needs_repaint, this, &SongListItem::needs_repaint);
}

auto
SongListItem::pop(const QString &uri) -> void
{
  hearts.pop(uri);
}

auto
SongListItem::set_playlists_available(bool available) -> void
{
  playlists_available = available;
  emit needs_repaint();
}

auto
SongListItem::time_col_width(const QAbstractItemModel *model) const -> int
{
  int rows = model ? model->rowCount() : 0;
  if (model == cached_model && rows == cached_rows)
    return cached_time_w;

  QFontMetrics fm{mono_font};
  int max_w = 0;
  for (int r = 0; r < rows; ++r)
    {
      auto item = model->index(r, 0).data(SongListModel::UserRoleSong).value<song>();
      max_w = std::max(max_w, fm.horizontalAdvance(timeutil::ms_to_text(item.duration)));
    }
  cached_model = model;
  cached_rows = rows;
  cached_time_w = max_w;
  return max_w;
}

auto
SongListItem::has_two_line_layout(const QModelIndex &index) const -> bool
{
  if (!index.data(SongListModel::UserRoleShowArtist).value<bool>())
    return false;
  auto item = index.data(SongListModel::UserRoleSong).value<song>();
  return !item.artist.isEmpty();
}

auto
SongListItem::add_rect(const QStyleOptionViewItem &option, const QModelIndex &index) const -> QRect
{
  if (!playlists_available)
    return {};
  if (index.data(SongListModel::UserRoleSkeleton).value<bool>())
    return {};

  auto content = track_rect_for(option, index).adjusted(ROW_PAD_H, ROW_PAD_V, -ROW_PAD_H, -ROW_PAD_V);
  int time_w = time_col_width(index.model());
  int heart_x = content.right() - time_w - COL_GAP - HEART_COL_W;
  int add_x = heart_x - COL_GAP - ADD_COL_W;
  int top_line_h = has_two_line_layout(index) ? content.height() / 2 : content.height();
  return QRect(add_x, content.top(), ADD_COL_W, top_line_h);
}

auto
SongListItem::track_rect_for(const QStyleOptionViewItem &option, const QModelIndex &index) const -> QRect
{
  auto rect = option.rect;
  rect.setTop(rect.top() + disc_header_height_at(index));
  return rect;
}

auto
SongListItem::disc_header_height_at(const QModelIndex &index) const -> int
{
  auto multi = index.data(SongListModel::UserRoleMultiDisc).value<bool>();
  auto opener = index.data(SongListModel::UserRoleDiscOpener).value<bool>();
  return (multi && opener) ? DISC_HEIGHT : 0;
}

auto
SongListItem::heart_rect(const QStyleOptionViewItem &option, const QModelIndex &index) const -> QRect
{
  if (index.data(SongListModel::UserRoleSkeleton).value<bool>())
    return {};
  if (!index.data(SongListModel::UserRoleFavoritesAvailable).value<bool>())
    return {}; // backend has no favorites support — no heart to hit

  auto content = track_rect_for(option, index).adjusted(ROW_PAD_H, ROW_PAD_V, -ROW_PAD_H, -ROW_PAD_V);
  int time_w = time_col_width(index.model());
  int heart_x = content.right() - time_w - COL_GAP - HEART_COL_W;
  int top_line_h = has_two_line_layout(index) ? content.height() / 2 : content.height();
  return QRect(heart_x, content.top(), HEART_COL_W, top_line_h);
}

auto
SongListItem::set_options(const SongListItemOpts &opts) -> void
{
  cover_size = opts.cover_size;
  margins = opts.margins;
}

auto
SongListItem::draw_label(QPainter *painter, const QRect &rect, const QFont &font, const QString &txt, bool elide) const -> void
{
  painter->save();
  painter->setFont(font);

  auto str = txt;
  if (elide)
    {
      QFontMetrics metrics{font};
      str = metrics.elidedText(txt, Qt::ElideRight, rect.width());
    }

  painter->drawText(rect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextDontClip, str);
  painter->restore();
}

auto
SongListItem::test_draw_disc_separator(const song &item, QPainter *painter, QRect rect, const QPalette &palette, const QFont &) const -> void
{
  painter->save();

  rect.setLeft(rect.left() + ROW_PAD_H);
  rect.setRight(rect.right() - ROW_PAD_H);

  auto label_font = theme::type::section_label();
  painter->setFont(label_font);

  auto dim2 = palette.color(QPalette::Mid);

  // "DISC N" label
  auto txt = QString("DISC %1").arg(item.disc_number);
  QFontMetrics fm{label_font};
  auto txt_w = fm.horizontalAdvance(txt);

  painter->setPen(dim2);
  painter->drawText(
      QRect(rect.left(), rect.top(), txt_w, rect.height()),
      Qt::AlignLeft | Qt::AlignVCenter, txt);

  // Hairline after label
  auto line_x = rect.left() + txt_w + 10;
  auto line_y = rect.center().y();
  painter->setPen(QPen(palette.color(QPalette::Midlight), 1));
  painter->drawLine(line_x, line_y, rect.right(), line_y);

  painter->restore();
}

auto
SongListItem::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const -> void
{
  painter->save();

  auto palette = option.palette;

  // Skeleton placeholder row shown while the real tracklist loads.
  if (index.data(SongListModel::UserRoleSkeleton).value<bool>())
    {
      auto content = option.rect.adjusted(ROW_PAD_H, ROW_PAD_V, -ROW_PAD_H, -ROW_PAD_V);
      QColor bar = palette.color(QPalette::Mid);
      bar.setAlpha(38);
      painter->setPen(Qt::NoPen);
      painter->setBrush(bar);
      painter->setRenderHint(QPainter::Antialiasing);
      // Pseudo-random-but-stable bar width so the rows don't look uniform.
      int span = content.width() - NUM_COL_W - COL_GAP - 40;
      int w = span / 2 + (index.row() * 37 % std::max(1, span / 2));
      int h = QFontMetrics{tfont_normal}.height() - 2;
      int y = content.top() + (content.height() - h) / 2;
      painter->drawRoundedRect(content.left(), y, NUM_COL_W, h, 2, 2);
      painter->drawRoundedRect(content.left() + NUM_COL_W + COL_GAP, y, w, h, 2, 2);
      painter->restore();
      return;
    }

  auto item = index.data(SongListModel::UserRoleSong).value<song>();
  auto playing = index.data(SongListModel::UserRoleCurrentlyPlaying).value<bool>();
  auto show_art = index.data(SongListModel::UserRoleShowArtist).value<bool>();
  auto favorited = index.data(SongListModel::UserRoleFavorited).value<bool>();
  auto fav_avail = index.data(SongListModel::UserRoleFavoritesAvailable).value<bool>();

  auto hover = option.state & QStyle::State_MouseOver;
  auto selected = option.state & QStyle::State_Selected;

  QRect full_rect = option.rect;
  QRect track_rect = full_rect;

  // Disc separator row at top
  int header_h = disc_header_height_at(index);
  if (header_h > 0)
    {
      auto disc_rect = QRect(full_rect.topLeft(), QSize(full_rect.width(), header_h));
      test_draw_disc_separator(item, painter, disc_rect, palette, mono_font);
      track_rect.setTop(full_rect.top() + header_h);
    }

  // Row background: selected > playing > hover > transparent. Selection wins
  // over the playing tint so a selected playing row reads as a normal solid
  // selection (and stays the playing track via the ▶ glyph) — otherwise its
  // light HighlightedText would land on the faint playing tint, unreadable.
  painter->setPen(Qt::NoPen);
  if (selected)
    {
      painter->fillRect(track_rect, palette.highlight());
    }
  else if (playing)
    {
      QColor bg = palette.color(QPalette::Highlight);
      bg.setAlpha(28);
      painter->fillRect(track_rect, bg);
    }
  else if (hover)
    {
      painter->fillRect(track_rect, palette.color(QPalette::Button));
    }

  // Content area (inset by horizontal padding)
  auto content = track_rect.adjusted(ROW_PAD_H, ROW_PAD_V, -ROW_PAD_H, -ROW_PAD_V);

  auto fg = selected ? palette.color(QPalette::HighlightedText) : palette.color(QPalette::WindowText);
  auto accent = palette.color(QPalette::Highlight);
  auto dim2 = palette.color(QPalette::Mid);

  QFontMetrics title_fm{tfont_normal};
  QFontMetrics artist_fm{afont};

  auto title_h = title_fm.height();
  auto artist_h = artist_fm.height();

  // Number/duration/heart/add all align to the top (title) line rather than
  // centering across the whole two-line row, so they read next to the title
  // instead of floating between it and the artist line below.
  bool two_line = show_art && !item.artist.isEmpty();
  int top_line_h = two_line ? content.height() / 2 : content.height();

  // Duration string, right-aligned within a column reserved for the track
  // list's widest value so a track crossing into an extra digit doesn't shift
  // the duration/heart/add columns of shorter rows.
  auto time_str = timeutil::ms_to_text(item.duration);
  auto time_w = time_col_width(index.model());
  auto time_rect = QRect(
      QPoint(content.right() - time_w, content.top()),
      QSize(time_w, top_line_h));

  painter->setFont(mono_font);
  painter->setPen(selected ? fg : (playing ? accent : dim2));
  painter->drawText(time_rect, Qt::AlignRight | Qt::AlignVCenter, time_str);

  // Track number column (right-aligned within NUM_COL_W)
  auto num_rect = QRect(content.topLeft(), QSize(NUM_COL_W, top_line_h));
  auto num_txt = playing
                     ? QString("▶")
                     : QString::asprintf("%02d", item.track_number);
  painter->setFont(mono_font);
  painter->setPen(selected ? fg : (playing ? accent : dim2));
  painter->drawText(num_rect, Qt::AlignRight | Qt::AlignVCenter, num_txt);

  int heart_x = content.right() - time_w - COL_GAP - HEART_COL_W;

  // Favorite heart: filled accent when on; faint outline on hover; hidden
  // otherwise (reveal-on-hover). Suppressed entirely when the backend has no
  // favorites support. Sits just left of the duration column.
  if (fav_avail && (favorited || hover))
    {
      auto column = QRectF(heart_x, content.top(), HEART_COL_W, top_line_h);
      auto scale = favorited ? hearts.scale_for(item.uri) : 1.0;
      favheart::paint(painter, favheart::box_in(column), favorited,
          selected ? fg : accent, selected ? fg : dim2, scale);
    }

  // Add-to-playlist "+" glyph: reveal-on-hover only (no persistent state),
  // just left of the heart. The column is always reserved in the title width
  // math below when playlists are supported, so hover doesn't reflow the row.
  if (playlists_available && hover)
    {
      int add_x = heart_x - COL_GAP - ADD_COL_W;
      QRectF column(add_x, content.top(), ADD_COL_W, top_line_h);
      auto c = column.center();
      qreal r = 4.5;
      painter->save();
      painter->setRenderHint(QPainter::Antialiasing);
      QPen pen(selected ? fg : dim2, 1.4);
      pen.setCapStyle(Qt::RoundCap);
      painter->setPen(pen);
      painter->drawLine(QPointF(c.x() - r, c.y()), QPointF(c.x() + r, c.y()));
      painter->drawLine(QPointF(c.x(), c.y() - r), QPointF(c.x(), c.y() + r));
      painter->restore();
    }

  // Title (and optional artist below for VA)
  int title_x = content.left() + NUM_COL_W + COL_GAP;
  int title_w = content.width() - NUM_COL_W - COL_GAP - HEART_COL_W - COL_GAP - time_w - COL_GAP;
  if (playlists_available)
    title_w -= ADD_COL_W + COL_GAP;

  if (two_line)
    {
      // Two-line: title on top half, artist below
      auto title_y = content.top() + (top_line_h - title_h) / 2;
      auto title_r = QRect(title_x, title_y, title_w, title_h);
      QFontMetrics fm{playing ? tfont_bold : tfont_normal};
      painter->setFont(playing ? tfont_bold : tfont_normal);
      painter->setPen(selected ? fg : (playing ? accent : fg));
      painter->drawText(title_r, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextDontClip,
          fm.elidedText(item.title, Qt::ElideRight, title_w));

      auto artist_y = content.top() + top_line_h + (top_line_h - artist_h) / 2;
      auto artist_r = QRect(title_x, artist_y, title_w, artist_h);
      QFontMetrics afm{afont};
      painter->setFont(afont);
      painter->setPen(selected ? fg : palette.color(QPalette::Dark));
      painter->drawText(artist_r, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextDontClip,
          afm.elidedText(item.artist, Qt::ElideRight, title_w));
    }
  else
    {
      auto title_r = QRect(title_x, content.top(), title_w, content.height());
      QFontMetrics fm{playing ? tfont_bold : tfont_normal};
      painter->setFont(playing ? tfont_bold : tfont_normal);
      painter->setPen(selected ? fg : (playing ? accent : fg));
      painter->drawText(title_r, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextDontClip,
          fm.elidedText(item.title, Qt::ElideRight, title_w));
    }

  painter->restore();
}

auto
SongListItem::sizeHint(const QStyleOptionViewItem &, const QModelIndex &index) const -> QSize
{
  auto show_art = index.data(SongListModel::UserRoleShowArtist).value<bool>();

  int title_h = QFontMetrics{tfont_normal}.height();
  int artist_h = QFontMetrics{afont}.height();

  // Row content height: single-line or two-line for VA
  int row_h = show_art
                  ? ROW_PAD_V + title_h + 4 + artist_h + ROW_PAD_V
                  : ROW_PAD_V + title_h + ROW_PAD_V;

  row_h += disc_header_height_at(index);

  // Width is the row's minimum content (number + a little title + duration),
  // kept well under the tracklist column's minimum. A size-hint width wider
  // than the viewport makes QListView lay rows out at the hint width behind a
  // horizontal scrollbar instead of stretching them to fit — which pushes the
  // right-aligned duration off the right edge when the panel is narrow.
  int time_w = QFontMetrics{mono_font}.horizontalAdvance("00:00");
  int min_w = ROW_PAD_H + NUM_COL_W + COL_GAP + 40 + COL_GAP + HEART_COL_W + COL_GAP + time_w + ROW_PAD_H;
  if (playlists_available)
    min_w += ADD_COL_W + COL_GAP;
  return QSize(min_w, row_h);
}
