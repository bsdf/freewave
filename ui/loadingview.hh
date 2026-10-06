#ifndef LOADINGVIEW_HH
#define LOADINGVIEW_HH

#include <QWidget>
#include <QString>

class QLabel;
class QFrame;
class QPushButton;
class FWMark;
class WaveSpinner;
class PulseDot;

class LoadingView : public QWidget {
  Q_OBJECT

public:
  enum class Phase { Connecting,
    Index,
    Rendering };

  explicit LoadingView(QWidget *parent = nullptr);

  void setPhase(Phase p);
  void setServerLabel(const QString &label);
  void setAlbumCount(int n);

signals:
  // Only reachable while Phase::Connecting — the app can hang here indefinitely
  // if the server never replies, with no other way back to settings.
  void cancel();

protected:
  void resizeEvent(QResizeEvent *event) override;

private:
  void updatePhaseText();

  Phase current_phase = Phase::Connecting;
  QString server_label;
  int album_count = -1;

  FWMark *logo = nullptr;
  QLabel *wordmark = nullptr;
  WaveSpinner *spinner = nullptr;
  QLabel *phase_label = nullptr;
  QLabel *sub_label = nullptr;

  QWidget *pip_container = nullptr;
  QFrame *pip1 = nullptr;
  QFrame *pip2 = nullptr;
  QFrame *pip3 = nullptr;

  QWidget *status_bar = nullptr;
  PulseDot *pulse_dot = nullptr;
  QLabel *server_label_widget = nullptr;

  QPushButton *cancel_btn = nullptr;
};

#endif // LOADINGVIEW_HH
