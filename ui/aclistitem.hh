#ifndef ALBUMCOVERLISTITEM_HH
#define ALBUMCOVERLISTITEM_HH

#include <memory>

#include <QFont>
#include <QSize>
#include <QMargins>
#include <QListView>
#include <QAbstractItemDelegate>
#include <QStyledItemDelegate>

struct AlbumCoverListItemOpts {
  QSize cover_size;
  QMargins margins;
  bool show_year;
};

class AlbumCoverListItem : public QAbstractItemDelegate {
  Q_OBJECT
public:
  explicit AlbumCoverListItem(QListView *parent_list, QObject *parent = nullptr);

public:
  auto tile_width() const -> int { return cover_size.width() + margins.left() + margins.right(); }

public slots:
  void set_options(const AlbumCoverListItemOpts &opts);
  void set_rounded_corners(bool enabled, int radius = 8);
  void set_drop_shadow(bool enabled);

protected:
  void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
  QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
  bool editorEvent(
      QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option, const QModelIndex &index) override;

private:
  QRect get_pixmap_rect(const QRect &cover_rect, const QPixmap &pixmap) const;
  void draw_label(QPainter *painter, const QRect &rect, const QFont &font, const QString &txt, bool elide = true) const;

  QSize cover_size;
  QMargins margins;

  QListView *parent_list;

  bool show_year = true;
  bool rounded_corners = false;
  int corner_radius = 8;
  bool drop_shadow = false;

  QFont title_font;
  QFont artist_font;
  QFont year_font;
};

#endif /* ALBUMCOVERLISTITEM_HH */
