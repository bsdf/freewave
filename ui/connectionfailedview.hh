#ifndef CONNECTIONFAILEDVIEW_HH
#define CONNECTIONFAILEDVIEW_HH

#include <QWidget>
#include <QVBoxLayout>

class QLabel;
class QResizeEvent;
class ErrorIcon;

class ConnectionFailedView : public QWidget {
  Q_OBJECT

public:
  enum class Kind { Offline,
    Timeout,
    Auth,
    Library }; // connected, but the library request failed

  explicit ConnectionFailedView(QWidget *parent = nullptr);

  void setError(Kind kind, const QString &detail);
  void setServerLabel(const QString &label);

signals:
  void retry();
  void openSettings();

protected:
  void resizeEvent(QResizeEvent *event) override;

private:
  QString server_label;
  QString error_detail;
  Kind current_kind = Kind::Offline;
  ErrorIcon *error_icon = nullptr;
  QLabel *title_label = nullptr;
  QLabel *sub_label = nullptr;
  QLabel *detail_chip = nullptr;
  QLabel *help_hint = nullptr;
  QVBoxLayout *card_layout = nullptr;

  void updateErrorIcon(Kind kind);
  void updateDetailChip();
  void updateLabels(Kind kind);
  void updateHelpHint();
  void updateWrappedLabelHeights();
};

#endif // CONNECTIONFAILEDVIEW_HH
