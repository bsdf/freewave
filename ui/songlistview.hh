#ifndef SONGLISTVIEW_HH
#define SONGLISTVIEW_HH

#include <QPoint>
#include <QListView>

#include "model/song.hh"

// Tracklist view. A QListView that, on top of the usual row activation, hit-tests
// the per-row favorite heart and add-to-playlist button drawn by SongListItem and
// turns a click on either into a signal (swallowing the event so it neither
// selects nor plays the row).
class SongListView : public QListView {
  Q_OBJECT
public:
  explicit SongListView(QWidget *parent = nullptr);

signals:
  void favorite_toggled(const QString &uri);
  void add_to_playlist_requested(const song &s);

protected:
  void mousePressEvent(QMouseEvent *event) override;
  void startDrag(Qt::DropActions supportedActions) override;

private:
  QPoint press_pos;
};

#endif /* SONGLISTVIEW_HH */
