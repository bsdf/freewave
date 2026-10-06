#include "aclistitem.hh"
#include "aclistmodel.hh"
#include "theme.hh"
#include "timeutil.hh"

#include <QFont>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QFontMetrics>

#define COVER_WIDTH  200
#define COVER_HEIGHT 200

#define MARGIN_LEFT   10
#define MARGIN_TOP    10
#define MARGIN_RIGHT  10
#define MARGIN_BOTTOM 4

AlbumCoverListItem::AlbumCoverListItem(QListView *parent_list, QObject *parent)
  : QAbstractItemDelegate(parent)
{
  this->parent_list = parent_list;

  set_options({QSize(COVER_WIDTH, COVER_HEIGHT),
      QMargins(MARGIN_LEFT, MARGIN_TOP, MARGIN_RIGHT, MARGIN_BOTTOM),
      true});
  title_font = theme::display_serif(17);
  artist_font = theme::ui_sans(12);
  year_font = theme::mono(10);
}

auto
AlbumCoverListItem::set_options(const AlbumCoverListItemOpts &opts) -> void
{
  cover_size = opts.cover_size;
  margins = opts.margins;
  show_year = opts.show_year;
}

auto
AlbumCoverListItem::set_rounded_corners(bool enabled, int radius) -> void
{
  rounded_corners = enabled;
  corner_radius = radius;
}

auto
AlbumCoverListItem::set_drop_shadow(bool enabled) -> void
{
  drop_shadow = enabled;
}

auto
AlbumCoverListItem::draw_label(QPainter *painter, const QRect &rect, const QFont &font, const QString &txt, bool elide) const -> void
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
AlbumCoverListItem::get_pixmap_rect(const QRect &cover_rect, const QPixmap &pixmap) const -> QRect
{
  // center the pixmap in case it's not a square
  auto w = pixmap.width();
  auto h = pixmap.height();

  auto ww = cover_size.width();
  auto hh = cover_size.height();

  auto diffw = ww - w;
  auto diffh = hh - h;

  auto marginw = diffw / 2;
  auto marginh = diffh / 2;

  return QRect(cover_rect.topLeft() + QPoint(marginw, marginh), QSize(w, h));
}

auto
AlbumCoverListItem::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const -> void
{
  painter->save();
  painter->setRenderHint(QPainter::Antialiasing);

  auto palette = option.palette;
  auto item = index.data(AlbumCoverListModel::UserRoleAlbum).value<album>();

  QFontMetrics title_metrics{title_font};
  QFontMetrics artist_metrics{artist_font};
  QFontMetrics year_metrics{year_font};
  auto title_height = title_metrics.height();
  auto secondary_height = artist_metrics.height();
  auto year_height = year_metrics.height();

  auto selected = option.state & QStyle::State_Selected;
  auto tw = tile_width();
  auto pad = (option.rect.width() - tw) / 2;
  auto rect = QRect(option.rect.topLeft() + QPoint(pad, 0), QSize(tw, option.rect.height())) - margins;

  auto pixmap = index.data(Qt::DecorationRole).value<QPixmap>();
  auto cover_rect = QRect(rect.topLeft(), cover_size);

  // selected state
  if (option.state & QStyle::State_Selected)
    {
      painter->setBrush(palette.highlight());
      painter->setPen(Qt::NoPen);
      painter->drawRect(option.rect);

      painter->setBrush(palette.highlightedText());
      painter->setPen(palette.color(QPalette::HighlightedText));
    }
  else
    {
      painter->setBrush(palette.text());
      painter->setPen(palette.color(QPalette::WindowText));
    }

  // draw cover
  auto pixmap_rect = get_pixmap_rect(cover_rect, pixmap);

  if (drop_shadow)
    {
      painter->save();
      painter->setPen(Qt::NoPen);
      const int offsets[] = {4, 3, 2};
      const int alphas[] = {40, 60, 80};
      for (int i = 0; i < 3; ++i)
        {
          int o = offsets[i];
          painter->setBrush(QColor(0, 0, 0, alphas[i]));
          auto shadow_rect = pixmap_rect.translated(o, o);
          if (rounded_corners)
            painter->drawRoundedRect(shadow_rect, corner_radius, corner_radius);
          else
            painter->drawRect(shadow_rect);
        }
      painter->restore();
    }

  if (rounded_corners)
    {
      painter->save();
      QPainterPath path;
      path.addRoundedRect(pixmap_rect, corner_radius, corner_radius);
      painter->setClipPath(path);
      painter->drawPixmap(pixmap_rect, pixmap);
      painter->restore();
    }
  else
    {
      painter->drawPixmap(pixmap_rect, pixmap);
    }

  // draw title
  auto title_gap = 10;
  auto title_rect = QRect(cover_rect.bottomLeft() + QPoint(0, title_gap), QSize(rect.width(), title_height));
  painter->setPen(selected ? palette.color(QPalette::HighlightedText) : palette.color(QPalette::WindowText));
  draw_label(painter, title_rect, title_font, item.name);

  // draw artist
  auto artist_rect = QRect(title_rect.bottomLeft() + QPoint(0, 3), QSize(rect.width(), secondary_height));
  if (!selected)
    painter->setPen(palette.color(QPalette::Dark));
  draw_label(painter, artist_rect, artist_font, item.artist);

  // draw year
  if (show_year)
    {
      auto year_rect = QRect(artist_rect.bottomLeft() + QPoint(0, 3), QSize(rect.width(), year_height));
      if (!selected)
        painter->setPen(palette.color(QPalette::Mid));
      draw_label(painter, year_rect, year_font, timeutil::year_label(item.date));
    }

  painter->restore();
}

auto
AlbumCoverListItem::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const -> QSize
{
  auto title_height = QFontMetrics{title_font}.height();
  auto artist_height = QFontMetrics{artist_font}.height();
  auto year_height = QFontMetrics{year_font}.height();

  auto tw = tile_width();
  auto title_gap = 10;
  auto tile_height = cover_size.height() + margins.top() + margins.bottom()
                     + title_gap + title_height + 3 + artist_height;

  if (show_year)
    tile_height += 3 + year_height;

  // With ScrollBarAlwaysOn, maximumViewportSize() already excludes the scrollbar width and
  // Qt's prepareItemsLayout sets verticalMargin=0 (no further adjustment). So we just need
  // the viewport width minus 1 to guard against Qt's >= wrap condition.
  // https://github.com/qt/qtbase/blob/7191b8fe38788ac57e15e4124955c3cd8333d858/src/widgets/itemviews/qlistview.cpp#L1800
  auto parent_width = parent_list->maximumViewportSize().width() - 1;

  auto new_spacing = 0;
  auto num_covers = parent_width / tw;

  if (num_covers > 0)
    {
      auto covered_space = num_covers * tw;
      auto empty_space = parent_width - covered_space;
      new_spacing = empty_space / num_covers;
    }

  return QSize(tw + new_spacing, tile_height);
}

auto
AlbumCoverListItem::editorEvent(
    QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option, const QModelIndex &index) -> bool
{
  spdlog::trace("aclistitem got editor event = {}", static_cast<int>(event->type()));
  return QAbstractItemDelegate::editorEvent(event, model, option, index);
}
