#ifndef NOWPLAYINGBADGE_HH
#define NOWPLAYINGBADGE_HH

#include <QString>
#include <QWidget>

class QLabel;
class QPixmap;
class song;

// The mini now-playing badge in the transport bar: cover thumbnail + title/artist,
// with a hover highlight and a pointing-hand cursor. Owns its own chrome and emits
// clicked() on release, so the host doesn't have to filter its mouse events.
class NowPlayingBadge : public QWidget {
  Q_OBJECT

public:
  explicit NowPlayingBadge(QWidget *parent = nullptr);

  void set_album_art(const QPixmap &pixmap);
  void set_song(const song &song);
  void update_fonts();

private:
  void apply_elision();

signals:
  void clicked();

protected:
  void mousePressEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;

private:
  QLabel *cover_label = nullptr;
  QLabel *title_label = nullptr;
  QLabel *artist_label = nullptr;
  QString full_title = QStringLiteral("Not Playing");
  QString full_artist;
};
#endif // NOWPLAYINGBADGE_HH
