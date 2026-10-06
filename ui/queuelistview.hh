#ifndef QUEUELISTVIEW_HH
#define QUEUELISTVIEW_HH

#include <memory>

#include <QListView>
#include <QPoint>
#include <QString>

#include "model/song.hh"
#include "queuelistitem.hh"

class QueueListView : public QListView {
  Q_OBJECT
public:
  explicit QueueListView(QWidget *parent = nullptr);

  auto set_display_mode(QueueListItem::QueueDisplayMode mode) -> void;
  auto set_queue_dark(bool dark) -> void;
  auto set_color_band(bool enabled) -> void;

  // Recompute row heights, not just repaint. Needed when data the delegate's
  // sizeHint reads (libman's album artist, behind UserRoleShowArtist) lands after
  // the rows were already laid out — the view caches those heights.
  auto refresh_layout() -> void;

  // Run the favorite "pop" bump on the row(s) with this uri.
  auto pop_favorite(const QString &uri) -> void;

  void set_playlists_available(bool available) { playlists_available = available; }

signals:
  void delete_items(const QList<QModelIndex> &indexes);
  void play_item(int pos);                   // jump playback to this queue position
  void favorite_toggled(const QString &uri); // heart clicked on a queue row
  void add_to_playlist(const song &s);       // open the add-to-playlist picker

protected:
  void keyPressEvent(QKeyEvent *event) override;
  void contextMenuEvent(QContextMenuEvent *event) override;
  void changeEvent(QEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void leaveEvent(QEvent *event) override;
  void startDrag(Qt::DropActions supportedActions) override;

private:
  // True when pos falls within a grouped-mode album header band (non-interactive).
  bool in_group_header(const QPoint &pos) const;

  std::unique_ptr<QueueListItem> list_item;
  QPoint press_pos;
  bool playlists_available = false;
};

#endif /* QUEUELISTVIEW_HH */
