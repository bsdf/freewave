#ifndef SONGLISTITEM_HH
#define SONGLISTITEM_HH

#include <memory>

#include <QFont>
#include <QSize>
#include <QRect>
#include <QString>
#include <QMargins>
#include <QAbstractItemDelegate>

#include "model/song.hh"
#include "ui/favoriteheart.hh"

class QAbstractItemModel;

struct SongListItemOpts {
  QSize cover_size;
  QMargins margins;
};

class SongListItem : public QAbstractItemDelegate {
  Q_OBJECT
public:
  explicit SongListItem(QObject *parent = nullptr);

  // Pixel rect of the favorite heart for this row — used by the view to hit-test
  // clicks. Empty for skeleton rows.
  auto heart_rect(const QStyleOptionViewItem &option, const QModelIndex &index) const -> QRect;

  // Height of the disc-separator band drawn above this row (0 if none). Used
  // by the view to keep the band non-interactive and to exclude it from the
  // drag pixmap when the row is dragged.
  auto disc_header_height_at(const QModelIndex &index) const -> int;

  // Pixel rect of the hover-revealed add-to-playlist button for this row (left
  // of the heart). Empty for skeleton rows or when playlists are unsupported.
  auto add_rect(const QStyleOptionViewItem &option, const QModelIndex &index) const -> QRect;

public slots:
  void set_options(const SongListItemOpts &opts);
  // Trigger the favorite "pop" bump on the row with this uri.
  void pop(const QString &uri);
  // Reveal the add-to-playlist button when the backend supports playlists.
  void set_playlists_available(bool available);

signals:
  void needs_repaint();

protected:
  void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
  QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
  QRect get_pixmap_rect(const QRect &cover_rect, const QPixmap &pixmap) const;
  void draw_label(QPainter *painter, const QRect &rect, const QFont &font, const QString &txt, bool elide) const;
  void test_draw_disc_separator(const song &item, QPainter *painter, QRect rect, const QPalette &palette, const QFont &font) const;
  QRect track_rect_for(const QStyleOptionViewItem &option, const QModelIndex &index) const;

  // Width reserved for the duration column: the widest duration string in the
  // whole track list, so a track crossing into an extra digit (e.g. 9:59 ->
  // 10:00) doesn't shove its own duration/heart/add columns left of the
  // shorter rows'. Cached; recomputed when the model or its row count changes.
  auto time_col_width(const QAbstractItemModel *model) const -> int;

  // True when this row draws title + artist on separate lines (VA albums).
  // The number/duration/heart/add columns align to the top (title) line in
  // that case, rather than centering across the whole two-line row.
  auto has_two_line_layout(const QModelIndex &index) const -> bool;

  QSize cover_size;
  QMargins margins;

  QFont tfont_normal;
  QFont tfont_bold;
  QFont afont;
  QFont mono_font;

  HeartPopAnimator hearts;
  bool playlists_available = false;

  mutable const QAbstractItemModel *cached_model = nullptr;
  mutable int cached_rows = -1;
  mutable int cached_time_w = 0;
};

#endif /* SONGLISTITEM_HH */
