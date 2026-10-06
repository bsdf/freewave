#ifndef ALBUMCOVERLISTVIEW_HH
#define ALBUMCOVERLISTVIEW_HH

#include "aclistitem.hh"
#include "model/album.hh"

#include <QList>
#include <QListView>
#include <QResizeEvent>
#include <QWheelEvent>

class AlbumCoverListView : public QListView {
  Q_OBJECT
public:
  explicit AlbumCoverListView(QWidget *parent = nullptr);

public slots:
  void set_cover_size(QSize size);
  void set_rounded_corners(bool enabled, int radius = 8);
  void set_drop_shadow(bool enabled);
  void set_playlists_available(bool available) { playlists_available = available; }

signals:
  void play_albums(const QList<album> &albums);      // replace queue, play
  void play_albums_next(const QList<album> &albums); // insert after current
  void queue_albums(const QList<album> &albums);     // append to queue
  void add_to_playlist(const album &a);              // open the add-to-playlist picker

protected:
  void resizeEvent(QResizeEvent *event) override;
  void contextMenuEvent(QContextMenuEvent *event) override;
  void changeEvent(QEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;

private:
  std::unique_ptr<AlbumCoverListItem> album_item;
  int left_margin = 0;
  bool playlists_available = false;
};

#endif // ALBUMCOVERLISTVIEW_HH
