#include "queuelistitem.hh"
#include "queuelistmodel.hh"
#include "timeutil.hh"
#include "theme.hh"

#include <QFont>
#include <QRectF>
#include <QPainter>
#include <QFontMetrics>
#include <QAbstractItemModel>

#include <algorithm>

#define TRACKNUM_COL_WIDTH 28

#define MARGIN_TOP    7
#define MARGIN_BOTTOM 7
#define MARGIN_LEFT   14
#define MARGIN_RIGHT  14

static constexpr int HEART_COL_W = 18; // favorite heart column (left of duration)
static constexpr int HEART_GAP = 8;    // gap flanking the heart column

QueueListItem::QueueListItem(QObject *parent)
  : QAbstractItemDelegate(parent)
{
  set_options({
      QSize(0, 0),
      QMargins(MARGIN_LEFT, MARGIN_TOP, MARGIN_RIGHT, MARGIN_BOTTOM),
  });
  tfont_playing = theme::type::track_title(true);
  tfont_normal = theme::type::track_title(false);
  mono_font = theme::type::mono_meta();
  sfont = theme::type::display_sm(false);
  group_album_font = theme::type::display_sm(true);
  group_artist_font = theme::mono(9);

  connect(&hearts, &HeartPopAnimator::needs_repaint, this, &QueueListItem::needs_repaint);
}

auto
QueueListItem::pop(const QString &uri) -> void
{
  hearts.pop(uri);
}

auto
QueueListItem::time_col_width(const QAbstractItemModel *model) const -> int
{
  int rows = model ? model->rowCount() : 0;
  if (model == cached_model && rows == cached_rows)
    return cached_time_w;

  QFontMetrics fm{mono_font};
  int max_w = 0;
  for (int r = 0; r < rows; ++r)
    {
      auto item = model->index(r, 0).data(QueueListModel::UserRoleSong).value<song>();
      max_w = std::max(max_w, fm.horizontalAdvance(timeutil::ms_to_text(item.duration)));
    }
  cached_model = model;
  cached_rows = rows;
  cached_time_w = max_w;
  return max_w;
}

auto
QueueListItem::heart_rect(const QStyleOptionViewItem &option, const QModelIndex &index) const -> QRect
{
  bool draw_header = (display_mode == QueueDisplayMode::Grouped)
                     && index.data(QueueListModel::UserRoleGroupStart).toBool();
  int header_h = draw_header ? group_header_height() : 0;
  if (!index.data(QueueListModel::UserRoleFavoritesAvailable).value<bool>())
    return {}; // backend has no favorites support — no heart to hit
  auto content_item_rect = option.rect.adjusted(0, header_h, 0, 0) - margins;
  int time_w = time_col_width(index.model());
  int heart_x = content_item_rect.right() - time_w - HEART_GAP - HEART_COL_W;

  // Match paint()'s tracknum_h: aligned to the title line when a subtitle is
  // present, rather than centered across the whole two-line row.
  bool playing = index.data(QueueListModel::UserRoleCurrentlyPlaying).value<bool>();
  QFontMetrics tfm{playing ? tfont_playing : tfont_normal};
  int line_h = has_subtitle(index) ? tfm.height() : content_item_rect.height();

  return QRect(heart_x, content_item_rect.top(), HEART_COL_W, line_h);
}

auto
QueueListItem::set_options(const QueueListItemOpts &opts) -> void
{
  cover_size = opts.cover_size;
  margins = opts.margins;
}

auto
QueueListItem::set_display_mode(QueueDisplayMode mode) -> void
{
  display_mode = mode;
  emit sizeHintChanged(QModelIndex());
}

auto
QueueListItem::set_dark(bool dark_) -> void
{
  dark = dark_;
}

auto
QueueListItem::set_color_band(bool enabled) -> void
{
  color_band = enabled;
}

auto
QueueListItem::set_hover_in_header(bool in_header) -> void
{
  hover_in_header = in_header;
}

auto
QueueListItem::draw_label(QPainter *painter, const QRect &rect, const QFont &font, const QString &txt, bool elide) const -> void
{
  painter->save();
  painter->setFont(font);

  auto str = txt;
  if (elide)
    {
      QFontMetrics metrics{font};
      str = metrics.elidedText(txt, Qt::ElideRight, rect.width());
    }

  painter->drawText(rect, Qt::AlignLeft | Qt::TextDontClip, str);
  painter->restore();
}

auto
QueueListItem::has_subtitle(const QModelIndex &index) const -> bool
{
  if (display_mode == QueueDisplayMode::Grouped)
    return index.data(QueueListModel::UserRoleShowArtist).toBool();

  if (display_mode == QueueDisplayMode::Full)
    return true;

  // Compact mode: show subtitle only if not same album as playing
  if (display_mode == QueueDisplayMode::Compact)
    return !index.data(QueueListModel::UserRolePlayingSameAlbum).toBool();

  return false;
}

auto
QueueListItem::header_height_at(const QModelIndex &index) const -> int
{
  bool is_header = (display_mode == QueueDisplayMode::Grouped)
                   && index.data(QueueListModel::UserRoleGroupStart).toBool();
  return is_header ? group_header_height() : 0;
}

auto
QueueListItem::group_header_height() const -> int
{
  return 10 + QFontMetrics{group_album_font}.height() + 1 + QFontMetrics{group_artist_font}.height() + 4;
}

auto
QueueListItem::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const -> void
{
  painter->save();

  auto palette = option.palette;

  auto item = index.data(QueueListModel::UserRoleSong).value<song>();
  auto playing = index.data(QueueListModel::UserRoleCurrentlyPlaying).value<bool>();
  auto favorited = index.data(QueueListModel::UserRoleFavorited).value<bool>();
  auto fav_avail = index.data(QueueListModel::UserRoleFavoritesAvailable).value<bool>();

  const auto &tfont = playing ? tfont_playing : tfont_normal;

  // Check if we need to draw a group header
  bool draw_header = (display_mode == QueueDisplayMode::Grouped) && index.data(QueueListModel::UserRoleGroupStart).toBool();
  int header_h = draw_header ? group_header_height() : 0;

  auto selected = option.state & QStyle::State_Selected;
  auto has_sub = has_subtitle(index);

  // Confine track content to below the header
  auto content_rect = option.rect.adjusted(0, header_h, 0, 0);
  auto content_item_rect = content_rect - margins;

  // Row background. Fill only the track content area (below any group header
  // band) so a selected/playing first track never tints its album header.
  // Selection wins over the playing tint: a selected playing row reads as a
  // normal selection (solid highlight) and stays the playing track via the ▶
  // glyph — otherwise it'd be the only faint patch among solid selected rows.
  if (selected)
    {
      painter->setBrush(palette.highlight());
      painter->setPen(Qt::NoPen);
      painter->drawRect(content_rect);
    }
  else if (playing)
    {
      // Themed accent tint (plum) — stronger on the dark panel for contrast.
      auto accent = palette.color(QPalette::Highlight);
      accent.setAlpha(dark ? 48 : 30);
      painter->setBrush(accent);
      painter->setPen(Qt::NoPen);
      painter->drawRect(content_rect);
    }
  else if ((option.state & QStyle::State_MouseOver) && !hover_in_header)
    {
      painter->setBrush(palette.color(QPalette::Button));
      painter->setPen(Qt::NoPen);
      painter->drawRect(content_rect);
    }

  // Color band: vertical accent bar on the left. Drawn last (after the
  // selection / playing / hover fills above) so no row state paints over it.
  // Uses content_rect, so in Grouped mode it starts below the album header
  // band rather than crossing it.
  if (color_band)
    {
      auto accent_color = index.data(QueueListModel::UserRoleAccent).value<QColor>();
      if (!accent_color.isValid())
        accent_color = palette.color(QPalette::Highlight);

      painter->setBrush(accent_color);
      painter->setPen(Qt::NoPen);
      painter->drawRect(content_rect.left(), content_rect.top(), 3, content_rect.height());
    }

  // Draw group header band if needed
  if (draw_header)
    {
      auto header_band = QRect(option.rect.left(), option.rect.top(), option.rect.width(), header_h);

      // Background wash
      painter->setBrush(dark ? QColor(255, 255, 255, 5) : QColor(0, 0, 0, 5));
      painter->setPen(Qt::NoPen);
      painter->drawRect(header_band);

      // Bottom hairline
      painter->setPen(palette.color(QPalette::Midlight));
      painter->drawLine(header_band.bottomLeft(), header_band.bottomRight());

      // Accent dot
      int dot_radius = 3;
      QFontMetrics album_fm{group_album_font};
      int album_line_top = header_band.top() + 10;
      int dot_y = album_line_top + album_fm.height() / 2;

      auto accent_color = index.data(QueueListModel::UserRoleAccent).value<QColor>();
      if (!accent_color.isValid())
        accent_color = palette.color(QPalette::Highlight);

      painter->setBrush(accent_color);
      painter->setPen(Qt::NoPen);
      painter->setRenderHint(QPainter::Antialiasing);
      painter->drawEllipse(QPointF(header_band.left() + MARGIN_LEFT + dot_radius, dot_y), dot_radius, dot_radius);
      painter->setRenderHint(QPainter::Antialiasing, false);

      // Album name
      int text_left = header_band.left() + MARGIN_LEFT + 2 * dot_radius + 8;
      auto album_name = index.data(QueueListModel::UserRoleAlbumName).toString();
      auto album_rect = QRect(text_left, album_line_top, header_band.right() - MARGIN_RIGHT - text_left, album_fm.height());
      painter->setPen(palette.color(QPalette::WindowText));
      draw_label(painter, album_rect, group_album_font, album_name, true);

      // Artist name
      QFontMetrics artist_fm{group_artist_font};
      int artist_line_top = album_line_top + album_fm.height() + 1;
      auto artist_rect = QRect(text_left, artist_line_top, header_band.right() - MARGIN_RIGHT - text_left, artist_fm.height());
      auto album_artist = index.data(QueueListModel::UserRoleAlbumArtist).toString();
      if (album_artist.isEmpty())
        album_artist = item.artist;
      painter->setPen(palette.color(QPalette::Dark));
      draw_label(painter, artist_rect, group_artist_font, album_artist, true);
    }

  // Track number column (28px, right-aligned, 10px mono). When a subtitle
  // line is present, align to the title line instead of centering across the
  // whole two-line row, so the number reads next to the title, not floating
  // between it and the subtitle.
  QFontMetrics tfm{tfont};
  int tracknum_h = has_sub ? tfm.height() : content_item_rect.height();
  auto tracknum_rect = QRect(content_item_rect.left(), content_item_rect.top(), TRACKNUM_COL_WIDTH, tracknum_h);

  int tracknum_value = 0;
  if (display_mode == QueueDisplayMode::Grouped)
    tracknum_value = index.data(QueueListModel::UserRoleLocalNumber).toInt() + 1;
  else
    tracknum_value = index.row() + 1;

  // Color precedence mirrors the background fill above: selection (solid
  // highlight) wins, so its text is HighlightedText; an unselected playing row
  // gets accent-colored text on its faint tint.
  auto tracknum_color = selected ? palette.color(QPalette::HighlightedText)
                                 : (playing ? palette.color(QPalette::Highlight) : palette.color(QPalette::Dark));
  painter->setPen(tracknum_color);
  QString tracknum_str = playing ? QString("▶") : QString("%1").arg(tracknum_value, 2, 10, QChar('0'));
  painter->setFont(mono_font);
  painter->drawText(tracknum_rect, Qt::AlignRight | Qt::AlignVCenter, tracknum_str);

  // Duration, right-aligned within a column reserved for the queue's widest
  // value so hour-long rows don't shift the duration/heart columns of
  // shorter rows (mono, dim). Aligned to the title line, same as the track
  // number, rather than centered across the whole two-line row.
  auto time_str = timeutil::ms_to_text(item.duration);
  auto time_w = time_col_width(index.model());
  auto time_rect = QRect(content_item_rect.right() - time_w, content_item_rect.top(), time_w, tracknum_h);
  painter->setPen(selected ? palette.color(QPalette::HighlightedText) : palette.color(QPalette::Dark));
  painter->setFont(mono_font);
  painter->drawText(time_rect, Qt::AlignRight | Qt::AlignVCenter, time_str);

  // Favorite heart: filled accent when on; faint outline on hover; hidden
  // otherwise (reveal-on-hover). Sits just left of the duration column,
  // aligned to the title line (tracknum_h) rather than centered across the
  // whole two-line row, same as the track number and duration above.
  bool heart_hover = (option.state & QStyle::State_MouseOver) && !hover_in_header;
  int heart_x = time_rect.left() - HEART_GAP - HEART_COL_W;
  if (fav_avail && (favorited || heart_hover))
    {
      auto accent = palette.color(QPalette::Highlight);
      auto fg = palette.color(QPalette::HighlightedText);
      auto dim = palette.color(QPalette::Dark);
      auto column = QRectF(heart_x, content_item_rect.top(), HEART_COL_W, tracknum_h);
      auto scale = favorited ? hearts.scale_for(item.uri) : 1.0;
      favheart::paint(painter, favheart::box_in(column), favorited,
          selected ? fg : accent, selected ? fg : dim, scale);
    }

  // Title and subtitle layout
  auto title_left = content_item_rect.left() + TRACKNUM_COL_WIDTH + 8;
  auto title_right = heart_x - HEART_GAP;
  auto title_color = selected ? palette.color(QPalette::HighlightedText)
                              : (playing ? palette.color(QPalette::Highlight) : palette.color(QPalette::WindowText));

  if (has_sub)
    {
      QFontMetrics sfm{sfont};
      int title_h = tfm.height();
      int subtitle_h = sfm.height();

      auto title_rect = QRect(title_left, content_item_rect.top(), title_right - title_left, title_h);
      painter->setPen(title_color);
      draw_label(painter, title_rect, tfont, item.title, true);

      // Subtitle
      auto subtitle_top = content_item_rect.top() + title_h + 1;
      auto subtitle_rect = QRect(title_left, subtitle_top, title_right - title_left, subtitle_h);

      QString subtitle_text;
      if (display_mode == QueueDisplayMode::Full)
        {
          auto album_artist = index.data(QueueListModel::UserRoleAlbumArtist).toString();
          if (album_artist.isEmpty())
            album_artist = item.artist;
          auto album_name = index.data(QueueListModel::UserRoleAlbumName).toString();
          subtitle_text = album_artist + " ～ " + album_name;
          painter->setFont(mono_font);
        }
      else if (display_mode == QueueDisplayMode::Compact || display_mode == QueueDisplayMode::Grouped)
        {
          subtitle_text = item.artist;
          painter->setFont(sfont);
        }

      painter->setPen(selected ? palette.color(QPalette::HighlightedText) : palette.color(QPalette::Dark));
      draw_label(painter, subtitle_rect, painter->font(), subtitle_text, true);
    }
  else
    {
      // No subtitle: vertically center the title
      auto title_rect = QRect(title_left, content_item_rect.top(), title_right - title_left, content_item_rect.height());
      painter->setPen(title_color);
      draw_label(painter, title_rect, tfont, item.title, true);
    }

  painter->restore();
}

auto
QueueListItem::sizeHint(const QStyleOptionViewItem &, const QModelIndex &index) const -> QSize
{
  int row_height = 0;
  QFontMetrics tfm{tfont_normal};

  if (has_subtitle(index))
    {
      QFontMetrics sfm{sfont};
      row_height = tfm.height() + sfm.height() + 1 + margins.top() + margins.bottom();
    }
  else
    {
      row_height = tfm.height() + margins.top() + margins.bottom();
    }

  // Add header height for grouped mode at group start
  bool draw_header = (display_mode == QueueDisplayMode::Grouped) && index.data(QueueListModel::UserRoleGroupStart).toBool();
  if (draw_header)
    {
      row_height += group_header_height();
    }

  return QSize(TRACKNUM_COL_WIDTH + margins.left() + margins.right(), row_height);
}
