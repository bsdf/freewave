#include "onboardingdialog.hh"
#include "controller/credentialstore.hh"
#include "controller/profilestore.hh"
#ifdef ENABLE_SUBSONIC
#include "controller/subsonicclient.hh"
#endif
#include "fwmark.hh"
#include "segmentedcontrol.hh"
#include "theme.hh"
#include "ui/theme/components.hh"
#include "wavespinner.hh"

#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>
#include <QTcpSocket>
#include <QUuid>
#include <QVBoxLayout>

// Blend color `c` at `alpha/255` opacity over white
static QColor
tint_over_white(QColor c, int alpha)
{
  return QColor(
      255 - (255 - c.red()) * alpha / 255,
      255 - (255 - c.green()) * alpha / 255,
      255 - (255 - c.blue()) * alpha / 255);
}

// ─── ConnTypeCard ─────────────────────────────────────────────────────────────

ConnTypeCard::ConnTypeCard(const QString &icon, const QString &title,
    const QString &desc, QWidget *parent)
  : QWidget(parent)
  , icon_text(icon)
  , title_text(title)
  , desc_text(desc)
{
  setFixedHeight(82);
  setCursor(Qt::PointingHandCursor);
}

void
ConnTypeCard::mousePressEvent(QMouseEvent *e)
{
  if (e->button() == Qt::LeftButton) emit clicked();
  QWidget::mousePressEvent(e);
}

void
ConnTypeCard::paintEvent(QPaintEvent *)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const auto &pal = palette();
  const int W = width(), H = height();

  const QColor border = active ? pal.highlight().color() : pal.midlight().color();
  const QColor bg = active ? tint_over_white(pal.highlight().color(), 24) : pal.base().color();
  p.setPen(QPen(border, 1.5));
  p.setBrush(bg);
  p.drawRoundedRect(QRectF(0.75, 0.75, W - 1.5, H - 1.5), 6, 6);

  // Badge: 36×36, 18px from left edge, vertically centered
  constexpr int BW = 36, BH = 36, BX = 18;
  const int BY = (H - BH) / 2;
  const QColor badge_bg = active ? pal.highlight().color() : pal.link().color();
  const QColor badge_fg = active ? pal.highlightedText().color() : pal.dark().color();
  p.setPen(Qt::NoPen);
  p.setBrush(badge_bg);
  p.drawRoundedRect(QRect(BX, BY, BW, BH), 6, 6);
  p.setPen(badge_fg);
  QFont mf = theme::mono(11);
  mf.setWeight(QFont::Medium);
  p.setFont(mf);
  p.drawText(QRect(BX, BY, BW, BH), Qt::AlignCenter, icon_text);

  const int TX = BX + BW + 14;
  QFont tf = theme::ui_sans(14);
  tf.setWeight(QFont::DemiBold);
  p.setFont(tf);
  p.setPen(active ? pal.highlight().color() : pal.windowText().color());
  p.drawText(QRect(TX, BY, W - TX - 18, 20), Qt::AlignLeft | Qt::AlignVCenter, title_text);

  p.setPen(pal.dark().color());
  p.setFont(theme::ui_sans(12));
  p.drawText(QRect(TX, BY + 22, W - TX - 18, H - (BY + 22) - 4),
      Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, desc_text);
}

// ─── StepDots ─────────────────────────────────────────────────────────────────

StepDots::StepDots(int total, QWidget *parent)
  : QWidget(parent)
  , total(total)
{
  setFixedHeight(10);
}

QSize
StepDots::sizeHint() const
{
  // Current step pill is 20px wide, others are 6px, gap is 5px between each
  return QSize(20 + (total - 1) * 11, 10);
}

void
StepDots::paintEvent(QPaintEvent *)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const auto &pal = palette();

  int x = 0;
  for (int i = 0; i < total; ++i)
    {
      const int w = (i == step) ? 20 : 6;
      const int y = (height() - 6) / 2;
      QColor c;
      if (i == step)
        c = pal.highlight().color();
      else if (i < step)
        c = tint_over_white(pal.highlight().color(), 80); // pale accent for done
      else
        c = pal.midlight().color();
      p.setPen(Qt::NoPen);
      p.setBrush(c);
      p.drawRoundedRect(QRect(x, y, w, 6), 3, 3);
      x += w + 5;
    }
}

// ─── OnboardingDialog helpers ─────────────────────────────────────────────────

QWidget *
OnboardingDialog::make_field(const QString &label, QLineEdit *edit, const QString &hint)
{
  auto *w = new QWidget;
  auto *vl = new QVBoxLayout(w);
  vl->setContentsMargins(0, 0, 0, 0);
  vl->setSpacing(5);

  auto *lbl = new QLabel(label.toUpper());
  QFont mf = theme::mono(9);
  mf.setLetterSpacing(QFont::AbsoluteSpacing, 2);
  lbl->setFont(mf);
  QPalette lp = lbl->palette();
  lp.setColor(QPalette::WindowText, QApplication::palette().color(QPalette::Mid));
  lbl->setPalette(lp);
  vl->addWidget(lbl);

  vl->addWidget(edit);

  if (!hint.isEmpty())
    {
      auto *hint_lbl = new QLabel(hint);
      hint_lbl->setFont(theme::ui_sans(11));
      QPalette hp = hint_lbl->palette();
      hp.setColor(QPalette::WindowText, QApplication::palette().color(QPalette::Mid));
      hint_lbl->setPalette(hp);
      vl->addWidget(hint_lbl);
    }

  return w;
}

QPushButton *
OnboardingDialog::make_primary_btn(const QString &text, QWidget *parent)
{
  auto *btn = new QPushButton(text, parent);
  btn->setFixedHeight(38);
  btn->setStyleSheet(R"(
    QPushButton {
      background: palette(highlight);
      color: palette(highlighted-text);
      border: none;
      border-radius: 4px;
    }
    QPushButton:pressed {
      background: palette(dark);
    }
    QPushButton:disabled {
      background: palette(midlight);
      color: palette(dark);
    }
  )");
  QFont f = theme::ui_sans(12);
  f.setWeight(QFont::Medium);
  f.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
  btn->setFont(f);
  return btn;
}

// ─── Page builders ────────────────────────────────────────────────────────────

QWidget *
OnboardingDialog::build_welcome()
{
  auto *page = new QWidget;
  auto *vl = new QVBoxLayout(page);
  vl->setContentsMargins(36, 32, 36, 32);
  vl->setSpacing(22);

  // Logo area (centered column: wave mark + title + subtitle)
  auto *logo_area = new QWidget;
  auto *logo_vl = new QVBoxLayout(logo_area);
  logo_vl->setContentsMargins(0, 8, 0, 8);
  logo_vl->setSpacing(10);
  logo_vl->setAlignment(Qt::AlignHCenter);

  auto *mark = new FWMark(36, logo_area);
  logo_vl->addWidget(mark, 0, Qt::AlignHCenter);

  auto *title = new QLabel("freewave");
  QFont tf = theme::ui_sans(34);
  tf.setWeight(QFont::DemiBold);
  tf.setLetterSpacing(QFont::AbsoluteSpacing, -0.8);
  title->setFont(tf);
  title->setAlignment(Qt::AlignCenter);
  logo_vl->addWidget(title);

  auto *subtitle = new QLabel("A music player for Linux");
  subtitle->setFont(theme::ui_sans(14));
  subtitle->setAlignment(Qt::AlignCenter);
  QPalette sp = subtitle->palette();
  sp.setColor(QPalette::WindowText, QApplication::palette().color(QPalette::Dark));
  subtitle->setPalette(sp);
  logo_vl->addWidget(subtitle);

  vl->addWidget(logo_area);

  auto *sep = theme::ui::separator(Qt::Horizontal);
  vl->addWidget(sep);

  auto *body = new QLabel(
#if defined(ENABLE_MPD) && defined(ENABLE_SUBSONIC)
      "Connect to MPD or a Subsonic-compatible server.\n"
#elif defined(ENABLE_SUBSONIC)
      "Connect to a Subsonic-compatible server.\n"
#else
      "Connect to a local or remote MPD server.\n"
#endif
      "We'll build your library on first run.");
  body->setFont(theme::ui_sans(13));
  body->setAlignment(Qt::AlignCenter);
  body->setWordWrap(true);
  QPalette bp = body->palette();
  bp.setColor(QPalette::WindowText, QApplication::palette().color(QPalette::Dark));
  body->setPalette(bp);
  vl->addWidget(body);

  auto *btn = make_primary_btn("GET STARTED", page);
  connect(btn, &QPushButton::clicked, this, &OnboardingDialog::on_get_started);
  vl->addWidget(btn);

  vl->addStretch();
  return page;
}

QWidget *
OnboardingDialog::build_choose()
{
  auto *page = new QWidget;
  auto *vl = new QVBoxLayout(page);
  vl->setContentsMargins(36, 32, 36, 32);
  vl->setSpacing(22);

  // Step label + title
  auto *step_lbl = new QLabel("STEP 1 OF 3");
  QFont mf = theme::mono(9);
  mf.setLetterSpacing(QFont::AbsoluteSpacing, 2);
  step_lbl->setFont(mf);
  QPalette mp = step_lbl->palette();
  mp.setColor(QPalette::WindowText, QApplication::palette().color(QPalette::Mid));
  step_lbl->setPalette(mp);

  auto *title = new QLabel("How do you want to connect?");
  QFont tf = theme::ui_sans(22);
  tf.setWeight(QFont::DemiBold);
  tf.setLetterSpacing(QFont::AbsoluteSpacing, -0.3);
  title->setFont(tf);

  auto *title_group = new QWidget;
  auto *tg_vl = new QVBoxLayout(title_group);
  tg_vl->setContentsMargins(0, 0, 0, 0);
  tg_vl->setSpacing(6);
  tg_vl->addWidget(step_lbl);
  tg_vl->addWidget(title);
  vl->addWidget(title_group);

  // Connection type cards
  auto *cards = new QWidget;
  auto *cards_vl = new QVBoxLayout(cards);
  cards_vl->setContentsMargins(0, 0, 0, 0);
  cards_vl->setSpacing(10);

  mpd_card = new ConnTypeCard("MPD", "Music Player Daemon",
      "Connect to a local or remote MPD server. Recommended for most Linux setups.", cards);
  sub_card = new ConnTypeCard("SUB", "Subsonic / Navidrome",
      "Stream from a self-hosted Subsonic-compatible server over the network.", cards);
  cards_vl->addWidget(mpd_card);
  cards_vl->addWidget(sub_card);
  vl->addWidget(cards);

  connect(mpd_card, &ConnTypeCard::clicked, this, [this]() { on_type_selected("mpd"); });
  connect(sub_card, &ConnTypeCard::clicked, this, [this]() { on_type_selected("subsonic"); });

  // Hide cards for backends that were not compiled in.
#ifndef ENABLE_MPD
  mpd_card->setVisible(false);
#endif
#ifndef ENABLE_SUBSONIC
  sub_card->setVisible(false);
#endif

  continue_btn = make_primary_btn("CONTINUE", page);
  continue_btn->setEnabled(false);
  connect(continue_btn, &QPushButton::clicked, this, &OnboardingDialog::on_continue);
  vl->addWidget(continue_btn);

  // If only one backend is available, pre-select it so the user can continue immediately.
#if defined(ENABLE_MPD) && !defined(ENABLE_SUBSONIC)
  on_type_selected("mpd");
#elif defined(ENABLE_SUBSONIC) && !defined(ENABLE_MPD)
  on_type_selected("subsonic");
#endif

  vl->addStretch();
  return page;
}

QWidget *
OnboardingDialog::build_details()
{
  auto *page = new QWidget;
  auto *vl = new QVBoxLayout(page);
  vl->setContentsMargins(36, 32, 36, 32);
  vl->setSpacing(22);

  // Step label + title (title text updated in go_to)
  auto *step_lbl = new QLabel("STEP 2 OF 3");
  QFont mf = theme::mono(9);
  mf.setLetterSpacing(QFont::AbsoluteSpacing, 2);
  step_lbl->setFont(mf);
  QPalette mp = step_lbl->palette();
  mp.setColor(QPalette::WindowText, QApplication::palette().color(QPalette::Mid));
  step_lbl->setPalette(mp);

  auto *details_title_label = new QLabel;
  QFont tf = theme::ui_sans(22);
  tf.setWeight(QFont::DemiBold);
  tf.setLetterSpacing(QFont::AbsoluteSpacing, -0.3);
  details_title_label->setFont(tf);
  // Store pointer for later update
  auto *title_group = new QWidget;
  auto *tg_vl = new QVBoxLayout(title_group);
  tg_vl->setContentsMargins(0, 0, 0, 0);
  tg_vl->setSpacing(6);
  tg_vl->addWidget(step_lbl);
  tg_vl->addWidget(details_title_label);
  vl->addWidget(title_group);

  // MPD form
  host_edit = new QLineEdit;
  host_edit->setObjectName("mpd_host");
  host_edit->setPlaceholderText("localhost");
  host_edit->setText("localhost");
  QFont hf = theme::mono(13);
  host_edit->setFont(hf);

  port_edit = new QLineEdit;
  port_edit->setObjectName("mpd_port");
  port_edit->setPlaceholderText("6600");
  port_edit->setText("6600");
  port_edit->setFont(hf);

  mpd_form = new QWidget;
  auto *mpd_vl = new QVBoxLayout(mpd_form);
  mpd_vl->setContentsMargins(0, 0, 0, 0);
  mpd_vl->setSpacing(14);
  mpd_vl->addWidget(
      make_field("Host", host_edit, "Hostname or IP of your MPD server"));
  mpd_vl->addWidget(make_field("Port", port_edit, "Default MPD port is 6600"));

  // Subsonic form
  ss_url_edit = new QLineEdit;
  ss_url_edit->setObjectName("ss_url");
  ss_url_edit->setPlaceholderText("https://music.example.com");
  ss_url_edit->setFont(hf);

  ss_api_key_edit = new QLineEdit;
  ss_api_key_edit->setObjectName("ss_api_key");
  ss_api_key_edit->setPlaceholderText("your-api-key");
  ss_api_key_edit->setFont(hf);
  auto *api_key_field = make_field("API Key", ss_api_key_edit, "From your server's user settings");

  ss_username_edit = new QLineEdit;
  ss_username_edit->setFont(hf);
  auto *username_field = make_field("Username", ss_username_edit);

  ss_password_edit = new QLineEdit;
  ss_password_edit->setFont(hf);
  ss_password_edit->setEchoMode(QLineEdit::Password);
  auto *password_field = make_field("Password", ss_password_edit);

  ss_auth_control = new SegmentedControl;
  auto *apikey_btn = ss_auth_control->add_segment(0, "API Key");
  ss_auth_control->add_segment(1, "Username/Password");
  apikey_btn->setChecked(true);

  sub_form = new QWidget;
  auto *sub_vl = new QVBoxLayout(sub_form);
  sub_vl->setContentsMargins(0, 0, 0, 0);
  sub_vl->setSpacing(14);
  sub_vl->addWidget(
      make_field("Server URL", ss_url_edit, "Full URL including protocol"));
  sub_vl->addWidget(ss_auth_control);
  sub_vl->addWidget(api_key_field);
  sub_vl->addWidget(username_field);
  sub_vl->addWidget(password_field);

  auto toggle_ss_auth_fields = [=, this] {
    bool userpass = (ss_auth_control->current() == 1);
    api_key_field->setVisible(!userpass);
    username_field->setVisible(userpass);
    password_field->setVisible(userpass);
  };
  connect(ss_auth_control, &SegmentedControl::selected, this,
      [=](int) { toggle_ss_auth_fields(); });
  toggle_ss_auth_fields();

  // Both forms added; only one shown at a time
  auto *forms_stack = new QStackedWidget;
  forms_stack->addWidget(mpd_form);
  forms_stack->addWidget(sub_form);
  vl->addWidget(forms_stack);

  // Store title label pointer via object name
  details_title_label->setObjectName("details_title");
  // Store forms_stack pointer via object name
  forms_stack->setObjectName("details_forms_stack");

  auto *connect_btn = make_primary_btn("CONNECT", page);
  connect(connect_btn, &QPushButton::clicked, this, &OnboardingDialog::on_connect);
  vl->addWidget(connect_btn);

  vl->addStretch();
  return page;
}

QWidget *
OnboardingDialog::build_connecting()
{
  auto *page = new QWidget;
  auto *vl = new QVBoxLayout(page);
  vl->setContentsMargins(36, 32, 36, 32);
  vl->setSpacing(22);
  vl->addStretch();

  auto *center = new QWidget;
  auto *cvl = new QVBoxLayout(center);
  cvl->setContentsMargins(0, 16, 0, 16);
  cvl->setSpacing(16);
  cvl->setAlignment(Qt::AlignHCenter);

  conn_spinner = new WaveSpinner(160, 28, center);
  cvl->addWidget(conn_spinner, 0, Qt::AlignHCenter);

  conn_label = new QLabel;
  conn_label->setObjectName("conn_label");
  {
    QFont clf = theme::ui_sans(14);
    clf.setWeight(QFont::Medium);
    conn_label->setFont(clf);
  }
  conn_label->setAlignment(Qt::AlignCenter);
  conn_label->setWordWrap(true);
  cvl->addWidget(conn_label);

  conn_detail = new QLabel("Establishing connection");
  conn_detail->setObjectName("conn_detail");
  QFont df = theme::mono(11);
  df.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
  conn_detail->setFont(df);
  conn_detail->setAlignment(Qt::AlignCenter);
  conn_detail->setWordWrap(true);
  QPalette dp = conn_detail->palette();
  dp.setColor(QPalette::WindowText, QApplication::palette().color(QPalette::Dark));
  conn_detail->setPalette(dp);
  cvl->addWidget(conn_detail);

  // Shown only when the connection check fails: the user is warned but can
  // still finish setup and fix the profile later (the app surfaces the same
  // error via the connect-failed overlay on launch).
  finish_anyway_btn = make_primary_btn("FINISH ANYWAY");
  finish_anyway_btn->setObjectName("finish_anyway_btn");
  finish_anyway_btn->setVisible(false);
  connect(finish_anyway_btn, &QPushButton::clicked, this, &OnboardingDialog::on_open);
  cvl->addWidget(finish_anyway_btn, 0, Qt::AlignHCenter);

  vl->addWidget(center);
  vl->addStretch();
  return page;
}

QWidget *
OnboardingDialog::build_done()
{
  auto *page = new QWidget;
  auto *vl = new QVBoxLayout(page);
  vl->setContentsMargins(36, 32, 36, 32);
  vl->setSpacing(22);

  // Check circle + title + subtitle (centered)
  auto *hero = new QWidget;
  auto *hero_vl = new QVBoxLayout(hero);
  hero_vl->setContentsMargins(0, 8, 0, 8);
  hero_vl->setSpacing(14);
  hero_vl->setAlignment(Qt::AlignHCenter);

  // Check circle: QLabel with border-radius renders the circle + checkmark
  auto *check = new QLabel("\xe2\x9c\x93");
  check->setFixedSize(52, 52);
  check->setAlignment(Qt::AlignCenter);
  check->setFont(theme::ui_sans(20));
  check->setStyleSheet(R"(
    QLabel {
      background: palette(window);
      border: 2px solid palette(highlight);
      border-radius: 26px;
      color: palette(highlight);
    }
  )");
  hero_vl->addWidget(check, 0, Qt::AlignHCenter);

  auto *done_title = new QLabel("You're all set");
  QFont tf = theme::ui_sans(22);
  tf.setWeight(QFont::DemiBold);
  tf.setLetterSpacing(QFont::AbsoluteSpacing, -0.3);
  done_title->setFont(tf);
  done_title->setAlignment(Qt::AlignCenter);
  hero_vl->addWidget(done_title);

  auto *done_sub = new QLabel("freewave will load your library when it opens.");
  done_sub->setFont(theme::ui_sans(13));
  done_sub->setAlignment(Qt::AlignCenter);
  done_sub->setWordWrap(true);
  QPalette dsp = done_sub->palette();
  dsp.setColor(QPalette::WindowText, QApplication::palette().color(QPalette::Dark));
  done_sub->setPalette(dsp);
  hero_vl->addWidget(done_sub);

  vl->addWidget(hero);
  vl->addSpacing(8);

  auto *open_btn = make_primary_btn("OPEN FREEWAVE", page);
  connect(open_btn, &QPushButton::clicked, this, &OnboardingDialog::on_open);
  vl->addWidget(open_btn);

  vl->addStretch();
  return page;
}

// ─── OnboardingDialog ─────────────────────────────────────────────────────────

OnboardingDialog::OnboardingDialog(QWidget *parent)
  : QDialog(parent)
{
  setWindowTitle("freewave \xe2\x80\x94 setup");
  setFixedSize(520, 520);

  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(30, 28, 30, 24);
  outer->setSpacing(0);

  // Header: logo mark + "freewave" name + step dots (right-aligned)
  auto *header = new QWidget;
  auto *header_hl = new QHBoxLayout(header);
  header_hl->setContentsMargins(0, 0, 0, 0);
  header_hl->setSpacing(0);

  auto *logo_row = new QWidget;
  auto *logo_hl = new QHBoxLayout(logo_row);
  logo_hl->setContentsMargins(0, 0, 0, 0);
  logo_hl->setSpacing(8);
  auto *mini_mark = new FWMark(14, logo_row);
  logo_hl->addWidget(mini_mark);
  auto *logo_lbl = new QLabel("freewave");
  QFont lf = theme::ui_sans(14);
  lf.setWeight(QFont::DemiBold);
  lf.setLetterSpacing(QFont::AbsoluteSpacing, -0.2);
  logo_lbl->setFont(lf);
  logo_hl->addWidget(logo_lbl);

  header_hl->addWidget(logo_row);
  header_hl->addStretch();
  dots = new StepDots(3, header);
  dots->setVisible(false);
  header_hl->addWidget(dots);

  outer->addWidget(header);
  outer->addSpacing(20);

  // Card frame
  auto *card = theme::ui::card();
  auto *card_vl = new QVBoxLayout(card);
  card_vl->setContentsMargins(0, 0, 0, 0);
  card_vl->setSpacing(0);

  stack = new QStackedWidget;
  stack->setFrameShape(QFrame::NoFrame);
  card_vl->addWidget(stack);

  stack->addWidget(build_welcome());    // 0
  stack->addWidget(build_choose());     // 1
  stack->addWidget(build_details());    // 2
  stack->addWidget(build_connecting()); // 3
  stack->addWidget(build_done());       // 4

  outer->addWidget(card, 1);
  outer->addSpacing(12);

  back_btn = theme::ui::back_button("Back");
  back_btn->setVisible(false);
  outer->addWidget(back_btn, 0, Qt::AlignHCenter);

  go_to(0);
}

// ─── Navigation ───────────────────────────────────────────────────────────────

void
OnboardingDialog::go_to(int page)
{
  stack->setCurrentIndex(page);

  // Steps: choose / details / connect. The done page has no dot.
  dots->setVisible(page > 0 && page < 4);
  if (page > 0 && page < 4)
    dots->setStep(page - 1);

  back_btn->setVisible(page == 2 || page == 3);
  QObject::disconnect(back_conn);
  if (page == 2)
    back_conn = connect(back_btn, &QPushButton::clicked, this, [this]() { go_to(1); });
  else if (page == 3)
    back_conn = connect(back_btn, &QPushButton::clicked, this, [this]() { go_to(2); });

  if (page == 2)
    {
      // Update title and visible form for selected connection type
      auto *title_lbl = stack->widget(2)->findChild<QLabel *>("details_title");
      auto *forms = stack->widget(2)->findChild<QStackedWidget *>("details_forms_stack");
      if (conn_type == "mpd")
        {
          if (title_lbl) title_lbl->setText("MPD server details");
          if (forms) forms->setCurrentIndex(0);
        }
      else
        {
          if (title_lbl) title_lbl->setText("Subsonic server details");
          if (forms) forms->setCurrentIndex(1);
        }
    }

  // Leaving the connection-check page cancels any in-flight probe.
  if (page != 3)
    teardown_probe();

  if (page == 3)
    start_probe();
}

// ─── Slots ────────────────────────────────────────────────────────────────────

void
OnboardingDialog::on_get_started()
{
  go_to(1);
}

void
OnboardingDialog::on_type_selected(const QString &t)
{
  conn_type = t;
  mpd_card->setActive(t == "mpd");
  sub_card->setActive(t == "subsonic");
  continue_btn->setEnabled(true);
}

void
OnboardingDialog::on_continue()
{
  go_to(2);
}

void
OnboardingDialog::on_connect()
{
  go_to(3);
}

// ─── Connection probe ──────────────────────────────────────────────────────────
//
// One authed request against the entered server — enough to catch a bad host, a
// refused connection, or a rejected credential without loading the whole
// library. On failure the user is warned but may still finish; the saved profile
// then reports the same error via the connect-failed overlay on launch.

void
OnboardingDialog::start_probe()
{
  teardown_probe(); // cancel a prior attempt if the user came back and retried

  conn_spinner->setVisible(true);
  finish_anyway_btn->setVisible(false);
  conn_detail->setText("Establishing connection");

  probe_timeout.setSingleShot(true);
  disconnect(&probe_timeout, nullptr, this, nullptr);
  connect(&probe_timeout, &QTimer::timeout, this,
      [this] { probe_failed("Timed out"); });

  if (conn_type == "mpd")
    {
      const auto host = host_edit->text().trimmed().isEmpty()
                            ? QString("localhost")
                            : host_edit->text().trimmed();
      const auto port_s = port_edit->text().trimmed();
      const quint16 port = port_s.isEmpty() ? 6600 : port_s.toUShort();
      conn_label->setText(QString("Connecting to MPD at %1:%2\xe2\x80\xa6").arg(host).arg(port));

      probe_socket = new QTcpSocket(this);
      connect(probe_socket, &QTcpSocket::readyRead, this, [this] {
        // MPD greets every new connection with a line "OK MPD <version>".
        if (probe_socket->readAll().startsWith("OK MPD"))
          probe_succeeded();
        else
          probe_failed("Not an MPD server");
      });
      connect(probe_socket, &QTcpSocket::errorOccurred, this,
          [this] { probe_failed(probe_socket->errorString()); });
      probe_socket->connectToHost(host, port);
    }
#ifdef ENABLE_SUBSONIC
  else
    {
      const auto url = ss_url_edit->text().trimmed();
      conn_label->setText(QString("Connecting to %1\xe2\x80\xa6")
              .arg(url.isEmpty() ? QString("server") : url));

      SubsonicAuth auth;
      const bool userpass = (ss_auth_control->current() == 1);
      if (userpass)
        {
          auth.username = ss_username_edit->text().trimmed();
          auth.password = ss_password_edit->text().trimmed();
        }
      else
        {
          auth.api_key = ss_api_key_edit->text().trimmed();
        }

      // Probe through the same client the app will use, so "can I reach this
      // server" is answered by the code that will be doing the reaching: same
      // auth and protocol version, same lenient JSON parse (LMS can serve
      // invalid UTF-8, which a strict parse rejects wholesale), same wording for
      // a failure. Same endpoint and request shape as the library load too, so
      // an auth failure surfaces here identically (getAlbumList2, size 1).
      probe_client = new SubsonicClient(url, auth, this);
      probe_client->get("getAlbumList2",
          {{"type", "alphabeticalByName"}, {"size", "1"}, {"offset", "0"}},
          [this](const SubsonicReply &r) {
            if (r.ok)
              probe_succeeded();
            else
              probe_failed(r.error);
          });
    }
#endif // ENABLE_SUBSONIC

  probe_timeout.start(15000);
}

void
OnboardingDialog::probe_succeeded()
{
  teardown_probe();
  conn_detail->setText("Connected");
  QTimer::singleShot(500, this, [this] { go_to(4); });
}

void
OnboardingDialog::probe_failed(const QString &reason)
{
  teardown_probe();
  conn_spinner->setVisible(false);
  conn_label->setText(QString("Couldn't connect: %1").arg(reason));
  conn_detail->setText("You can finish setup and fix this later in Settings.");
  finish_anyway_btn->setVisible(true);
  // Back (to the details form) is already shown and wired by go_to for page 3.
}

void
OnboardingDialog::teardown_probe()
{
  probe_timeout.stop();
#ifdef ENABLE_SUBSONIC
  if (probe_client)
    {
      // Drops the handler before the reply can fire it at a dialog that has
      // moved on (retry, or the user going back).
      probe_client->abort_all();
      probe_client->deleteLater();
      probe_client = nullptr;
    }
#endif
  if (probe_socket)
    {
      probe_socket->abort();
      probe_socket->deleteLater();
      probe_socket = nullptr;
    }
}

void
OnboardingDialog::on_open()
{
  BackendProfile profile{
      .id = QUuid::createUuid().toString(QUuid::WithoutBraces),
      .type = conn_type,
      .name = conn_type == "mpd" ? "MPD" : "Subsonic"};

  if (conn_type == "mpd")
    {
      QString h = host_edit->text().trimmed();
      profile.params["host"] = h.isEmpty() ? "localhost" : h;
      QString ps = port_edit->text().trimmed();
      profile.params["port"] = ps.isEmpty() ? 6600u : ps.toUInt();
    }
  else if (conn_type == "subsonic")
    {
      profile.params["url"] = ss_url_edit->text().trimmed();
      bool userpass = (ss_auth_control->current() == 1);
      profile.params["auth_mode"] = userpass ? "userpass" : "apikey";
      if (userpass)
        profile.params["username"] = ss_username_edit->text().trimmed();
#ifdef ENABLE_KEYCHAIN
      CredentialStore cs;
      // Trim: a stray leading/trailing space (classic paste artifact) changes
      // the salted-token hash and gets rejected — and a server whose ping
      // doesn't validate auth surfaces it only as an opaque data-endpoint error.
      auto stored = cs.write_blocking(profile.id,
          userpass ? "password" : "api_key",
          userpass ? ss_password_edit->text().trimmed()
                   : ss_api_key_edit->text().trimmed());
      if (!stored)
        {
          QMessageBox::warning(this, "Couldn't Save Credential",
              QStringLiteral("The API key/password couldn't be stored in the OS "
                             "keyring, so the profile was not saved.\n\n%1")
                  .arg(stored.error()));
          return; // don't persist or close
        }
#endif
    }

  ProfileStore store;
  store.add(profile);
  store.set_active_id(profile.id);
  QSettings().setValue("setup_complete", true);
  accept();
}
