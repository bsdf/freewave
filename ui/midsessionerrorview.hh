#ifndef MIDSESSIONERRORVIEW_HH
#define MIDSESSIONERRORVIEW_HH

#include <QWidget>
#include <QString>

class QLabel;
class QFrame;
class QVBoxLayout;
class QPushButton;
class ErrorIcon;
class FWMark;

class MidSessionErrorView : public QWidget {
  Q_OBJECT

public:
  enum class Kind { Gone,
    Offline,
    Timeout };

  explicit MidSessionErrorView(QWidget *parent = nullptr);

  void setError(Kind kind);
  void setServerLabel(const QString &label);
  void setWasPlaying(bool playing);

signals:
  void reconnect();
  void dismiss();

protected:
  void paintEvent(QPaintEvent *) override;
  void resizeEvent(QResizeEvent *) override;

private:
  void updateErrorIcon();

  Kind currentKind = Kind::Gone;
  QString serverLabel;
  bool wasPlaying = false;

  QFrame *card = nullptr;
  ErrorIcon *errorIcon = nullptr;
  QLabel *titleLabel = nullptr;
  QLabel *subLabel = nullptr;
  QLabel *serverChipLabel = nullptr;
  QPushButton *reconnectBtn = nullptr;
  QPushButton *dismissBtn = nullptr;
  QVBoxLayout *errorIconLayout = nullptr;
};

#endif // MIDSESSIONERRORVIEW_HH
