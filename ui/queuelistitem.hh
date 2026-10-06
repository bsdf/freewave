#ifndef QUEUELISTITEM_HH
#define QUEUELISTITEM_HH

#include <memory>

#include <QFont>
#include <QSize>
#include <QRect>
#include <QString>
#include <QMargins>
#include <QAbstractItemDelegate>

#include "ui/favoriteheart.hh"

class QAbstractItemModel;

struct QueueListItemOpts {
  QSize cover_size;
  QMargins margins;
};

class QueueListItem : public QAbstractItemDelegate {
  Q_OBJECT
public:
  enum class QueueDisplayMode { Compact,
    Full,
    Grouped };

  explicit QueueListItem(QObject *parent = nullptr);

public:
  // Height of the group-header band drawn above this row (0 if none). Used by
  // the view to keep the header band non-interactive.
  int header_height_at(const QModelIndex &index) const;

  // When true, the row currently under the cursor is being hovered over its
  // group-header band, so the track content below must not draw a hover fill.
  void set_hover_in_header(bool in_header);

  // Pixel rect of the favorite heart for this row — used by the view to hit-test
  // clicks.
  QRect heart_rect(const QStyleOptionViewItem &option, const QModelIndex &index) const;

public slots:
  void set_options(const QueueListItemOpts &opts);
  void set_display_mode(QueueDisplayMode mode);
  void set_dark(bool dark);
  void set_color_band(bool enabled);
  // Trigger the favorite "pop" bump on the row with this uri.
  void pop(const QString &uri);

signals:
  void needs_repaint();

protected:
  void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
  QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
  void draw_label(QPainter *painter, const QRect &rect, const QFont &font, const QString &txt, bool elide) const;
  bool has_subtitle(const QModelIndex &index) const;
  int group_header_height() const;

  // Width reserved for the duration column: the widest duration string in the
  // whole queue, so a row that reaches the hours segment (h:mm:ss) doesn't
  // shove its own duration + heart left of the shorter rows'. Cached;
  // recomputed when the model or its row count changes.
  auto time_col_width(const QAbstractItemModel *model) const -> int;

  QSize cover_size;
  QMargins margins;

  QFont tfont_normal;
  QFont tfont_playing;
  QFont mono_font;
  QFont sfont;
  QFont group_album_font;
  QFont group_artist_font;

  QueueDisplayMode display_mode = QueueDisplayMode::Grouped;
  bool dark = false;
  bool color_band = false;
  bool hover_in_header = false;

  HeartPopAnimator hearts;

  mutable const QAbstractItemModel *cached_model = nullptr;
  mutable int cached_rows = -1;
  mutable int cached_time_w = 0;
};

#endif /* QUEUELISTITEM_HH */
