#ifndef ALBUMVIEW_H
#define ALBUMVIEW_H

#include <QWidget>
#include <QModelIndex>
#include <QList>

#include "controller/albumartmanager.hh"
#include "ui/songlistmodel.hh"
#include "ui/songlistitem.hh"
#include "model/album.hh"
#include "model/song.hh"

namespace Ui {
class AlbumView;
}

class FavoritesManager;

class AlbumView : public QWidget {
  Q_OBJECT

public:
  explicit AlbumView(std::shared_ptr<AlbumArtManager> artman,
      std::shared_ptr<FavoritesManager> favman, QWidget *parent = nullptr);
  ~AlbumView();

public slots:
  void set_data(album a, const QList<song> &s);
  void set_current_song(const song &song);
  void songlist_activated(const QModelIndex &index);
  // Gate the add-to-playlist header button, tracklist hover button, and
  // context-menu item on the backend supporting playlists.
  void set_playlists_available(bool available);

signals:
  void close();
  void activated(uint32_t pos, QList<song> songs);
  void play_songs_next(QList<song> songs); // insert after current
  void queue_songs(QList<song> songs);
  void add_album_to_playlist(album a);
  void add_song_to_playlist(song s);

protected:
  void changeEvent(QEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void paintEvent(QPaintEvent *event) override;

private slots:
  void update_fonts();
  void songlist_context_menu(const QPoint &pos);

private:
  std::unique_ptr<Ui::AlbumView> ui;

  album selected_album;
  SongListModel *song_model;

  std::shared_ptr<AlbumArtManager> artman;
  std::shared_ptr<FavoritesManager> favman;
  std::unique_ptr<SongListItem> song_item;

  bool playlists_available = false;
};

#endif // ALBUMVIEW_H
