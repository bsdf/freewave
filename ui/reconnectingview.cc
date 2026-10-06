#include "reconnectingview.hh"
#include "statewidgets.hh"
#include "theme.hh"
#include "ui/theme/components.hh"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

ReconnectingView::ReconnectingView(QWidget *parent)
  : QWidget(parent)
{
  setAutoFillBackground(false);

  // Card frame: Base bg, radius 8, no border — floats over the dimmed scrim
  card = theme::ui::card(this, false);

  auto *card_layout = new QHBoxLayout(card);
  card_layout->setContentsMargins(16, 16, 20, 16);
  card_layout->setSpacing(16);

  // Left: CountdownRing (44x44)
  ring = new CountdownRing(44, card);

  // Center: text column
  auto *text_widget = new QWidget(card);
  auto *text_layout = new QVBoxLayout(text_widget);
  text_layout->setContentsMargins(0, 0, 0, 0);
  text_layout->setSpacing(3);

  auto *title_label = new QLabel("Reconnecting…", text_widget);
  title_label->setFont(theme::ui_sans(13));
  title_label->setForegroundRole(QPalette::WindowText);

  attempt_label = new QLabel(text_widget);
  attempt_label->setFont(theme::mono(10));
  attempt_label->setForegroundRole(QPalette::Dark);
  attempt_label->setStyleSheet("letter-spacing: 0.4px;");

  text_layout->addWidget(title_label);
  text_layout->addWidget(attempt_label);
  text_widget->setLayout(text_layout);

  // Right: action buttons
  auto *button_widget = new QWidget(card);
  auto *button_layout = new QHBoxLayout(button_widget);
  button_layout->setContentsMargins(0, 0, 0, 0);
  button_layout->setSpacing(6);

  auto *now_btn = theme::ui::primary_button("NOW", button_widget);
  connect(now_btn, &QPushButton::clicked, this, &ReconnectingView::retryNow);

  auto *cancel_btn = theme::ui::outline_pill_button("CANCEL", button_widget);
  connect(cancel_btn, &QPushButton::clicked, this, &ReconnectingView::giveUp);

  button_layout->addWidget(now_btn);
  button_layout->addWidget(cancel_btn);
  button_widget->setLayout(button_layout);

  // Assemble card layout
  card_layout->addWidget(ring, 0, Qt::AlignVCenter);
  card_layout->addWidget(text_widget, 1, Qt::AlignVCenter);
  card_layout->addWidget(button_widget, 0, Qt::AlignVCenter);
  card->setLayout(card_layout);

  card->setMinimumWidth(360);

  // Root layout: stretch above card + bottom margin
  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);
  root->addStretch();
  root->addWidget(card, 0, Qt::AlignCenter);
  root->addSpacing(72);
  setLayout(root);
}

void
ReconnectingView::setAttempt(int n, int max)
{
  attempt_n = n;
  attempt_max = max;
  refresh_attempt();
}

void
ReconnectingView::refresh_attempt()
{
  attempt_label->setText(
      QString("Attempt %1/%2 ～ %3").arg(attempt_n).arg(attempt_max).arg(server_label));
}

void
ReconnectingView::setSeconds(int s)
{
  ring->setSeconds(s);
}

void
ReconnectingView::setTotal(int t)
{
  ring->setTotal(t);
}

void
ReconnectingView::setServerLabel(const QString &label)
{
  server_label = label;
  refresh_attempt();
}

void
ReconnectingView::paintEvent(QPaintEvent *)
{
  // Paint translucent dark wash: rgba(10,15,24,0.55)
  QPainter p(this);
  p.fillRect(rect(), QColor(10, 15, 24, 140));
}
