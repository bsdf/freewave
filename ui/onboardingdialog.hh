#ifndef ONBOARDINGDIALOG_HH
#define ONBOARDINGDIALOG_HH

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QWidget>

class WaveSlider;
class WaveSpinner;
class SegmentedControl;
class SubsonicClient;
class QTcpSocket;

// Clickable connection-type selection card
class ConnTypeCard : public QWidget {
  Q_OBJECT
public:
  ConnTypeCard(const QString &icon, const QString &title, const QString &desc,
      QWidget *parent = nullptr);
  void setActive(bool a)
  {
    active = a;
    update();
  }
  bool isActive() const { return active; }
  QSize sizeHint() const override { return QSize(0, 82); }
  QSize minimumSizeHint() const override { return sizeHint(); }
signals:
  void clicked();

protected:
  void mousePressEvent(QMouseEvent *) override;
  void paintEvent(QPaintEvent *) override;

private:
  QString icon_text, title_text, desc_text;
  bool active = false;
};

// Animated step-progress dots (pill for current step, circles for others)
class StepDots : public QWidget {
  Q_OBJECT
public:
  explicit StepDots(int total, QWidget *parent = nullptr);
  void setStep(int s)
  {
    step = s;
    update();
  }
  QSize sizeHint() const override;
  QSize minimumSizeHint() const override { return sizeHint(); }

protected:
  void paintEvent(QPaintEvent *) override;

private:
  int step = 0, total;
};

class OnboardingDialog : public QDialog {
  Q_OBJECT
public:
  explicit OnboardingDialog(QWidget *parent = nullptr);

private slots:
  void on_get_started();
  void on_type_selected(const QString &t);
  void on_continue();
  void on_connect();
  void on_open();

private:
  void go_to(int page);

  // Probe the entered server with one authed request. On failure the user is
  // warned but can still finish setup and fix the profile later.
  void start_probe();
  void probe_succeeded();
  void probe_failed(const QString &reason);
  void teardown_probe();

  QWidget *build_welcome();
  QWidget *build_choose();
  QWidget *build_details();
  QWidget *build_connecting();
  QWidget *build_done();

  static QWidget *make_field(const QString &label, QLineEdit *edit,
      const QString &hint = {});
  static QPushButton *make_primary_btn(const QString &text,
      QWidget *parent = nullptr);

  QStackedWidget *stack;
  StepDots *dots;
  QPushButton *back_btn;

  // Step 1
  ConnTypeCard *mpd_card;
  ConnTypeCard *sub_card;
  QPushButton *continue_btn;

  // Step 2
  QWidget *mpd_form;
  QWidget *sub_form;
  QLineEdit *host_edit;
  QLineEdit *port_edit;
  QLineEdit *ss_url_edit;
  SegmentedControl *ss_auth_control;
  QLineEdit *ss_api_key_edit;
  QLineEdit *ss_username_edit;
  QLineEdit *ss_password_edit;

  // Step 3 — connection check
  WaveSpinner *conn_spinner;
  QLabel *conn_label;
  QLabel *conn_detail;
  QPushButton *finish_anyway_btn;

  // Connection probe — owned, torn down after each attempt. The Subsonic side
  // probes through a real SubsonicClient so the check matches what the app will
  // actually do; MPD's greeting is a bare socket read.
#ifdef ENABLE_SUBSONIC
  SubsonicClient *probe_client = nullptr;
#endif
  QTcpSocket *probe_socket = nullptr;
  QTimer probe_timeout;

  QString conn_type;
  QMetaObject::Connection back_conn; // current Back-button handler, per page
};

#endif // ONBOARDINGDIALOG_HH
