#include "loadingview.hh"
#include "fwmark.hh"
#include "wavespinner.hh"
#include "statewidgets.hh"
#include "theme.hh"
#include "ui/theme/components.hh"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QVBoxLayout>

LoadingView::LoadingView(QWidget *parent)
  : QWidget(parent)
{
  setAutoFillBackground(true);
  setBackgroundRole(QPalette::Window);

  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  // Center content: logo + spinner + labels + pips
  auto *center = new QWidget(this);
  auto *center_layout = new QVBoxLayout(center);
  center_layout->setContentsMargins(0, 0, 0, 0);
  center_layout->setSpacing(28);
  center_layout->addStretch();

  // Logo column (small, centered)
  auto *logo_container = new QWidget(this);
  auto *logo_layout = new QVBoxLayout(logo_container);
  logo_layout->setContentsMargins(0, 0, 0, 0);
  logo_layout->setSpacing(10);
  logo_layout->setAlignment(Qt::AlignCenter);

  logo = new FWMark(28, logo_container);
  logo_layout->addWidget(logo, 0, Qt::AlignCenter);

  wordmark = new QLabel("freewave", logo_container);
  auto wordmark_font = theme::ui_sans(15);
  wordmark_font.setWeight(QFont::DemiBold);
  wordmark_font.setLetterSpacing(QFont::AbsoluteSpacing, -0.2);
  wordmark->setFont(wordmark_font);
  logo_layout->addWidget(wordmark, 0, Qt::AlignCenter);

  center_layout->addWidget(logo_container, 0, Qt::AlignCenter);

  // Spinner — same height as the logo mark above so the wave strokes match
  spinner = new WaveSpinner(160, 28, this);
  center_layout->addWidget(spinner, 0, Qt::AlignCenter);

  // Phase label + sub label
  auto *phase_container = new QWidget(this);
  auto *phase_layout = new QVBoxLayout(phase_container);
  phase_layout->setContentsMargins(0, 0, 0, 0);
  phase_layout->setSpacing(5);
  phase_layout->setAlignment(Qt::AlignCenter);

  phase_label = new QLabel(phase_container);
  phase_label->setFont(theme::ui_sans(13));
  phase_label->setAlignment(Qt::AlignCenter);
  phase_layout->addWidget(phase_label);

  sub_label = new QLabel(phase_container);
  sub_label->setFont(theme::mono(11));
  sub_label->setForegroundRole(QPalette::Dark);
  sub_label->setAlignment(Qt::AlignCenter);
  sub_label->setStyleSheet("letter-spacing: 0.5px;");
  phase_layout->addWidget(sub_label);

  center_layout->addWidget(phase_container, 0, Qt::AlignCenter);

  // Phase pips (three rounded rects with animated transitions)
  pip_container = new QWidget(this);
  auto *pip_layout = new QHBoxLayout(pip_container);
  pip_layout->setContentsMargins(0, 0, 0, 0);
  pip_layout->setSpacing(6);
  pip_layout->setAlignment(Qt::AlignCenter);

  pip1 = new QFrame(pip_container);
  pip1->setFixedSize(6, 6);
  pip1->setFrameShape(QFrame::NoFrame);
  pip1->setAutoFillBackground(true);
  pip1->setStyleSheet("border-radius: 3px;");
  pip_layout->addWidget(pip1);

  pip2 = new QFrame(pip_container);
  pip2->setFixedSize(6, 6);
  pip2->setFrameShape(QFrame::NoFrame);
  pip2->setAutoFillBackground(true);
  pip2->setStyleSheet("border-radius: 3px;");
  pip_layout->addWidget(pip2);

  pip3 = new QFrame(pip_container);
  pip3->setFixedSize(6, 6);
  pip3->setFrameShape(QFrame::NoFrame);
  pip3->setAutoFillBackground(true);
  pip3->setStyleSheet("border-radius: 3px;");
  pip_layout->addWidget(pip3);

  center_layout->addWidget(pip_container, 0, Qt::AlignCenter);

  cancel_btn = theme::ui::outline_pill_button("CANCEL", this);
  connect(cancel_btn, &QPushButton::clicked, this, &LoadingView::cancel);
  center_layout->addWidget(cancel_btn, 0, Qt::AlignCenter);

  center_layout->addStretch();
  root->addWidget(center, 1);

  // Status bar: fixed at bottom, 32px height
  status_bar = new QWidget(this);
  status_bar->setFixedHeight(32);
  status_bar->setAutoFillBackground(true);
  status_bar->setBackgroundRole(QPalette::Base);

  auto *status_layout = new QHBoxLayout(status_bar);
  status_layout->setContentsMargins(16, 0, 16, 0);
  status_layout->setSpacing(8);

  pulse_dot = new PulseDot(6, status_bar);
  status_layout->addWidget(pulse_dot, 0, Qt::AlignVCenter);

  server_label_widget = new QLabel(status_bar);
  server_label_widget->setFont(theme::mono(10));
  server_label_widget->setForegroundRole(QPalette::Dark);
  server_label_widget->setStyleSheet("letter-spacing: 0.5px;");
  status_layout->addWidget(server_label_widget, 0, Qt::AlignVCenter);

  status_layout->addStretch();

  // A selector-less QSS declaration applies to a widget AND all its
  // descendants, so a "border-top" set on status_bar itself would also
  // border every QFrame-derived child (the server label). The hairline
  // must be a real separator widget instead.
  root->addWidget(theme::ui::separator(Qt::Horizontal, this), 0);
  root->addWidget(status_bar, 0);

  setPhase(Phase::Connecting);
}

void
LoadingView::setPhase(Phase p)
{
  current_phase = p;
  updatePhaseText();
  cancel_btn->setVisible(p == Phase::Connecting);

  int phase_idx = static_cast<int>(p);

  auto set_pip = [this](QFrame *pip, int idx, int active_idx) {
    if (idx < active_idx)
      {
        // Completed: faded accent (40% opacity)
        pip->setFixedSize(6, 6);
        QColor accent = palette().color(QPalette::Highlight);
        accent.setAlpha(102); // ~40%
        auto p = palette();
        p.setColor(QPalette::Window, accent);
        pip->setPalette(p);
      }
    else if (idx == active_idx)
      {
        // Active: full-width accent (20×6)
        pip->setFixedSize(20, 6);
        auto p = palette();
        p.setColor(QPalette::Window, p.color(QPalette::Highlight));
        pip->setPalette(p);
      }
    else
      {
        // Future: hairline (6×6)
        pip->setFixedSize(6, 6);
        auto p = palette();
        p.setColor(QPalette::Window, p.color(QPalette::Midlight));
        pip->setPalette(p);
      }
  };

  set_pip(static_cast<QFrame *>(pip1), 0, phase_idx);
  set_pip(static_cast<QFrame *>(pip2), 1, phase_idx);
  set_pip(static_cast<QFrame *>(pip3), 2, phase_idx);
}

void
LoadingView::setServerLabel(const QString &label)
{
  server_label = label;
  server_label_widget->setText(label);
  if (current_phase == Phase::Connecting)
    {
      updatePhaseText();
    }
}

void
LoadingView::setAlbumCount(int n)
{
  album_count = n;
  if (current_phase == Phase::Index)
    {
      updatePhaseText();
    }
}

void
LoadingView::updatePhaseText()
{
  switch (current_phase)
    {
    case Phase::Connecting:
      phase_label->setText("Connecting to server");
      sub_label->setText(server_label);
      break;
    case Phase::Index:
      phase_label->setText("Loading library");
      if (album_count >= 0)
        {
          sub_label->setText(QString::number(album_count) + " albums");
        }
      else
        {
          sub_label->setText("Reading index…");
        }
      break;
    case Phase::Rendering:
      phase_label->setText("Almost there");
      sub_label->setText("Preparing your library");
      break;
    }
}

void
LoadingView::resizeEvent(QResizeEvent *event)
{
  QWidget::resizeEvent(event);
}
