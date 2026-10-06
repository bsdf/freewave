#include "playlistsidebaritem.hh"
#include "playlistsidebarmodel.hh"
#include "covermosaic.hh"
#include "controller/albumartmanager.hh"

#include <QFontMetrics>
#include <QPainter>
#include <QStringList>

#include "theme.hh"

namespace {
constexpr int MARGIN_L = 14;
constexpr int MARGIN_R = 12;
constexpr int MARGIN_V = 9;
constexpr int TILE = PLAYLIST_SIDEBAR_TILE;
constexpr int GAP = 11;
} // namespace

PlaylistSidebarItem::PlaylistSidebarItem(AlbumArtManager *art, QObject *parent)
  : QAbstractItemDelegate{parent}
  , art{art}
{
  name_font = theme::type::body(true);
  meta_font = theme::mono(9);
}

QSize
PlaylistSidebarItem::sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const
{
  return QSize(0, TILE + 2 * MARGIN_V);
}

void
PlaylistSidebarItem::paint(QPainter *painter, const QStyleOptionViewItem &option,
    const QModelIndex &index) const
{
  painter->save();
  const auto &palette = option.palette;

  bool selected = option.state & QStyle::State_Selected;
  bool hover = option.state & QStyle::State_MouseOver;

  // Selected rows read as an accent tint (not a solid highlight) to match the
  // quiet sidebar surface.
  if (selected)
    {
      auto tint = palette.color(QPalette::Highlight);
      tint.setAlpha(38);
      painter->fillRect(option.rect, tint);
      painter->fillRect(option.rect.left(), option.rect.top(), 3, option.rect.height(),
          palette.color(QPalette::Highlight));
    }
  else if (hover)
    {
      painter->fillRect(option.rect, palette.color(QPalette::Button));
    }

  auto content = option.rect.adjusted(MARGIN_L, MARGIN_V, -MARGIN_R, -MARGIN_V);
  auto tile = QRect(content.left(), content.top(), TILE, TILE);

  bool is_auto = index.data(PlaylistSidebarModel::UserRoleIsAuto).toBool();
  if (is_auto)
    covermosaic::paint_heart(painter, tile, palette.color(QPalette::Highlight),
        palette.color(QPalette::HighlightedText));
  else if (art)
    {
      auto id = index.data(PlaylistSidebarModel::UserRolePlaylistId).toString();
      auto stamp = index.data(PlaylistSidebarModel::UserRoleMosaicStamp).toString();
      auto hashes = index.data(PlaylistSidebarModel::UserRoleMosaicHashes).toStringList();
      auto pm = art->get_playlist_mosaic(id, stamp, hashes, tile.size());
      if (pm.isNull()) // contents not loaded yet → quiet placeholder
        painter->fillRect(tile, palette.color(QPalette::Midlight));
      else
        painter->drawPixmap(tile.topLeft(), pm);
    }

  int text_left = tile.right() + GAP;
  int text_w = content.right() - text_left;

  QFontMetrics nfm{name_font};
  QFontMetrics mfm{meta_font};
  int block_h = nfm.height() + 2 + mfm.height();
  int top = content.top() + (content.height() - block_h) / 2;

  auto name = index.data(PlaylistSidebarModel::UserRoleName).toString();
  auto name_rect = QRect(text_left, top, text_w, nfm.height());
  painter->setFont(name_font);
  painter->setPen(palette.color(QPalette::WindowText));
  painter->drawText(name_rect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextDontClip,
      nfm.elidedText(name, Qt::ElideRight, text_w));

  auto meta = index.data(PlaylistSidebarModel::UserRoleMeta).toString();
  auto meta_rect = QRect(text_left, top + nfm.height() + 2, text_w, mfm.height());
  painter->setFont(meta_font);
  painter->setPen(palette.color(QPalette::PlaceholderText));
  painter->drawText(meta_rect, Qt::AlignLeft | Qt::AlignVCenter,
      mfm.elidedText(meta, Qt::ElideRight, text_w));

  painter->restore();
}
