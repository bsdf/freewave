#ifndef STALELIBRARYBANNER_HH
#define STALELIBRARYBANNER_HH

#include <QWidget>
#include <QString>

class QLabel;
class QPushButton;

class StaleLibraryBanner : public QWidget {
  Q_OBJECT

public:
  explicit StaleLibraryBanner(QWidget *parent = nullptr);

public slots:
  void setAlbumCount(int n);
  void setLastSync(const QString &text);

signals:
  void retry();
  void dismissed();

protected:
  void paintEvent(QPaintEvent *) override;

private:
  QLabel *message_label;
  QPushButton *retry_btn;
  QPushButton *dismiss_btn;
  int album_count = 0;
  QString last_sync;

  void rebuild_message();
};

#endif // STALELIBRARYBANNER_HH
