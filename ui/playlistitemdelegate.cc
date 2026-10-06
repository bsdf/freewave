#include "playlistitemdelegate.hh"
#include "playlistitemmodel.hh"

#include <algorithm>

#include <QAbstractItemModel>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

#include "controller/albumartmanager.hh"
#include "model/song.hh"
#include "theme.hh"
#include "timeutil.hh"

namespace {
constexpr int MARGIN_L = 14;
constexpr int MARGIN_R = 14;
constexpr int MARGIN_V = 9;
constexpr int INDEX_COL = 22;
constexpr int COVER = 36;
constexpr int GAP = 12;
constexpr int HEART_COL_W = 18;
constexpr int HEART_GAP = 8;
} // namespace

PlaylistItemDelegate::PlaylistItemDelegate(AlbumArtManager *art, QObject *parent)
  : QAbstractItemDelegate{parent}
  , art{art}
{
  title_font = theme::type::track_title(false);
  sub_font = theme::type::display_sm(false);
  mono_font = theme::type::mono_meta();
  chip_font = theme::mono(8);
  chip_font.setWeight(QFont::Medium);
  chip_font.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
  connect(&hearts, &HeartPopAnimator::needs_repaint, this, &PlaylistItemDelegate::needs_repaint);
}

void
PlaylistItemDelegate::pop(const QString &uri)
{
  hearts.pop(uri);
}

QSize
PlaylistItemDelegate::sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const
{
  return QSize(0, COVER + 2 * MARGIN_V);
}

auto
PlaylistItemDelegate::time_col_width(const QAbstractItemModel *model) const -> int
{
  int rows = model ? model->rowCount() : 0;
  if (model == cached_model && rows == cached_rows)
    return cached_time_w;

  QFontMetrics fm{mono_font};
  int max_w = 0;
  for (int r = 0; r < rows; ++r)
    {
      auto dur = model->index(r, 0).data(PlaylistItemModel::UserRoleDurationMs).toLongLong();
      max_w = std::max(max_w, fm.horizontalAdvance(timeutil::ms_to_text(uint32_t(dur))));
    }
  cached_model = model;
  cached_rows = rows;
  cached_time_w = max_w;
  return max_w;
}

auto
PlaylistItemDelegate::heart_rect(const QStyleOptionViewItem &option, const QModelIndex &index) const -> QRect
{
  bool is_album = index.data(PlaylistItemModel::UserRoleKind).toInt() == 1;
  bool fav_avail = index.data(PlaylistItemModel::UserRoleFavoritesAvailable).toBool();
  if (is_album || !fav_avail)
    return {};
  auto content = option.rect.adjusted(MARGIN_L, MARGIN_V, -MARGIN_R, -MARGIN_V);
  int time_w = time_col_width(index.model());
  int heart_x = content.right() - time_w - HEART_GAP - HEART_COL_W;
  return QRect(heart_x, content.top(), HEART_COL_W, content.height());
}

void
PlaylistItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
    const QModelIndex &index) const
{
  painter->save();
  const auto &palette = option.palette;

  bool is_album = index.data(PlaylistItemModel::UserRoleKind).toInt() == 1;
  bool selected = option.state & QStyle::State_Selected;
  bool playing = index.data(PlaylistItemModel::UserRoleCurrentlyPlaying).toBool();
  bool hover = option.state & QStyle::State_MouseOver;

  // Row background: selection wins, else a faint accent tint for the playing
  // row, else a hover fill (mirrors the queue/tracklist convention).
  if (selected)
    {
      painter->fillRect(option.rect, palette.highlight());
    }
  else if (playing)
    {
      auto accent = palette.color(QPalette::Highlight);
      accent.setAlpha(30);
      painter->fillRect(option.rect, accent);
    }
  else if (hover)
    {
      painter->fillRect(option.rect, palette.color(QPalette::Button));
    }

  auto content = option.rect.adjusted(MARGIN_L, MARGIN_V, -MARGIN_R, -MARGIN_V);

  auto dim = selected ? palette.color(QPalette::HighlightedText) : palette.color(QPalette::Dark);
  auto fg = selected ? palette.color(QPalette::HighlightedText) : palette.color(QPalette::WindowText);
  auto accent = selected ? palette.color(QPalette::HighlightedText) : palette.color(QPalette::Highlight);

  // Index / play marker.
  auto index_rect = QRect(content.left(), content.top(), INDEX_COL, content.height());
  painter->setFont(mono_font);
  painter->setPen(playing && !selected ? accent : dim);
  QString num = playing ? QStringLiteral("▶")
                        : QString("%1").arg(index.row() + 1, 2, 10, QChar('0'));
  painter->drawText(index_rect, Qt::AlignLeft | Qt::AlignVCenter, num);

  // Cover thumb.
  int cover_x = index_rect.right() + 6;
  auto cover_rect = QRect(cover_x, content.top() + (content.height() - COVER) / 2, COVER, COVER);
  auto hash = index.data(PlaylistItemModel::UserRoleAlbumHash).toString();
  QPixmap pm = art ? art->get_art(hash, QSize(COVER, COVER)) : QPixmap{};
  if (!pm.isNull())
    {
      painter->save();
      QPainterPath clip;
      clip.addRoundedRect(cover_rect, 3, 3);
      painter->setClipPath(clip);
      painter->drawPixmap(cover_rect, pm);
      painter->restore();
    }
  else
    {
      painter->fillRect(cover_rect, palette.color(QPalette::Midlight));
    }

  // Duration, right-aligned within a column reserved for the list's widest value
  // so hour-long rows don't shift the duration/heart columns of shorter rows.
  auto dur = index.data(PlaylistItemModel::UserRoleDurationMs).toLongLong();
  auto time_str = timeutil::ms_to_text(uint32_t(dur));
  int time_w = time_col_width(index.model());
  auto time_rect = QRect(content.right() - time_w, content.top(), time_w, content.height());
  painter->setFont(mono_font);
  painter->setPen(dim);
  painter->drawText(time_rect, Qt::AlignRight | Qt::AlignVCenter, time_str);

  // Favorite heart (Track rows), just left of the duration.
  bool fav_avail = index.data(PlaylistItemModel::UserRoleFavoritesAvailable).toBool();
  bool favorited = index.data(PlaylistItemModel::UserRoleFavorited).toBool();
  int text_right = time_rect.left() - GAP;
  if (!is_album && fav_avail)
    {
      int heart_x = time_rect.left() - HEART_GAP - HEART_COL_W;
      if (favorited || hover)
        {
          auto uri = index.data(PlaylistItemModel::UserRoleSong).value<song>().uri;
          auto on = selected ? palette.color(QPalette::HighlightedText) : palette.color(QPalette::Highlight);
          auto off = selected ? palette.color(QPalette::HighlightedText) : palette.color(QPalette::Dark);
          auto column = QRectF(heart_x, content.top(), HEART_COL_W, content.height());
          auto scale = favorited ? hearts.scale_for(uri) : 1.0;
          favheart::paint(painter, favheart::box_in(column), favorited, on, off, scale);
        }
      text_right = heart_x - HEART_GAP;
    }

  // Text block: primary line + secondary line, vertically centered.
  int text_left = cover_rect.right() + GAP;
  auto primary = index.data(PlaylistItemModel::UserRolePrimary).toString();
  auto secondary = index.data(PlaylistItemModel::UserRoleSecondary).toString();

  QFontMetrics tfm{title_font};
  QFontMetrics sfm{sub_font};
  int block_h = tfm.height() + 1 + sfm.height();
  int top = content.top() + (content.height() - block_h) / 2;

  int primary_left = text_left;
  // Album rows lead with an ALBUM chip.
  if (is_album)
    {
      QFontMetrics cfm{chip_font};
      QString chip = QStringLiteral("ALBUM");
      constexpr int PAD_H = 6, PAD_V = 2, GLYPH_GAP = 4;
      int text_w = cfm.horizontalAdvance(chip);
      int glyph_d = cfm.ascent();
      int chip_w = PAD_H + glyph_d + GLYPH_GAP + text_w + PAD_H;
      int chip_h = cfm.height() + 2 * PAD_V;
      QRect chip_rect(text_left, top + (tfm.height() - chip_h) / 2, chip_w, chip_h);

      painter->save();
      painter->setRenderHint(QPainter::Antialiasing, true);
      // Filled accent-tint pill (no border) — mirrors --accent-t in the mockup.
      auto tint = selected ? palette.color(QPalette::HighlightedText)
                           : palette.color(QPalette::Highlight);
      tint.setAlpha(selected ? 48 : 32);
      painter->setPen(Qt::NoPen);
      painter->setBrush(tint);
      painter->drawRoundedRect(chip_rect, 3, 3);

      // Vinyl-record glyph: two concentric rings keeping the SVG's r=9/r=2.4
      // proportion, sized to fill the glyph box and optically centered on the
      // caps band (the geometric row centre sits below all-caps text).
      qreal outer_r = glyph_d / 2.0;
      qreal inner_r = outer_r * (2.4 / 9.0);
      qreal cx = chip_rect.left() + PAD_H + outer_r;
      qreal cy = chip_rect.top() + PAD_V + cfm.ascent() - cfm.capHeight() / 2.0;
      // Filled accent disc with a punched-out centre hole (odd-even fill leaves
      // the inner circle unpainted, so the pill tint beneath shows through).
      QPainterPath disc;
      disc.setFillRule(Qt::OddEvenFill);
      disc.addEllipse(QPointF(cx, cy), outer_r, outer_r);
      disc.addEllipse(QPointF(cx, cy), inner_r, inner_r);
      painter->setPen(Qt::NoPen);
      painter->setBrush(accent);
      painter->drawPath(disc);

      int text_x = chip_rect.left() + PAD_H + glyph_d + GLYPH_GAP;
      QRect chip_text_rect(text_x, chip_rect.top(), chip_rect.right() - text_x, chip_rect.height());
      painter->setFont(chip_font);
      painter->setPen(accent);
      painter->drawText(chip_text_rect, Qt::AlignLeft | Qt::AlignVCenter, chip);
      painter->restore();
      primary_left = chip_rect.right() + 8;
    }

  auto primary_rect = QRect(primary_left, top, text_right - primary_left, tfm.height());
  painter->setFont(title_font);
  painter->setPen(playing && !selected ? accent : fg);
  painter->drawText(primary_rect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextDontClip,
      tfm.elidedText(primary, Qt::ElideRight, primary_rect.width()));

  auto sub_rect = QRect(text_left, top + tfm.height() + 1, text_right - text_left, sfm.height());
  painter->setFont(sub_font);
  painter->setPen(dim);
  painter->drawText(sub_rect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextDontClip,
      sfm.elidedText(secondary, Qt::ElideRight, sub_rect.width()));

  painter->restore();
}
