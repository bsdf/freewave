#ifndef RECONNECTINGVIEW_HH
#define RECONNECTINGVIEW_HH

#include <QWidget>
#include <QString>

class QLabel;
class QFrame;
class CountdownRing;

class ReconnectingView : public QWidget {
  Q_OBJECT

public:
  explicit ReconnectingView(QWidget *parent = nullptr);

public slots:
  void setAttempt(int n, int max);
  void setSeconds(int s);
  void setTotal(int t);
  void setServerLabel(const QString &label);

signals:
  void retryNow();
  void giveUp();

protected:
  void paintEvent(QPaintEvent *) override;

private:
  void refresh_attempt();

  QFrame *card;
  CountdownRing *ring;
  QLabel *attempt_label;
  QString server_label;
  int attempt_n = 1;
  int attempt_max = 5;
};

#endif // RECONNECTINGVIEW_HH
