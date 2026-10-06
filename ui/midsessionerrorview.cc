#include "midsessionerrorview.hh"
#include "fwmark.hh"
#include "statewidgets.hh"
#include "theme.hh"
#include "ui/theme/components.hh"

#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QResizeEvent>

MidSessionErrorView::MidSessionErrorView(QWidget *parent)
  : QWidget(parent)
{
  setWindowFlags(windowFlags() | Qt::FramelessWindowHint);

  // Card frame (Base bg, radius 8, no border — floats over the dimmed scrim)
  card = theme::ui::card(this, false);
  card->setFixedWidth(380);

  auto *cardLayout = new QVBoxLayout(card);
  cardLayout->setContentsMargins(0, 0, 0, 0);
  cardLayout->setSpacing(0);

  // ── Header row ────────────────────────────────────────────────────────
  auto *headerWidget = new QWidget(card);
  auto *headerLayout = new QHBoxLayout(headerWidget);
  headerLayout->setContentsMargins(16, 16, 20, 14);
  headerLayout->setSpacing(12);

  auto *fwMark = new FWMark(14, headerWidget);
  headerLayout->addWidget(fwMark);

  auto *fwLabel = new QLabel("freewave", headerWidget);
  auto fwFont = theme::ui_sans(13);
  fwFont.setWeight(QFont::DemiBold);
  fwLabel->setFont(fwFont);
  fwLabel->setForegroundRole(QPalette::WindowText);
  fwLabel->setStyleSheet("letter-spacing: -0.2px;");
  headerLayout->addWidget(fwLabel);

  headerLayout->addStretch();

  auto *dismissHeaderBtn = new QPushButton("✕", headerWidget);
  dismissHeaderBtn->setFlat(true);
  dismissHeaderBtn->setFixedSize(21, 21);
  dismissHeaderBtn->setFont(theme::ui_sans(13));
  dismissHeaderBtn->setStyleSheet(
      "QPushButton { color: palette(mid); background: transparent; border: none; "
      "padding: 0; }");
  connect(dismissHeaderBtn, &QPushButton::clicked, this, &MidSessionErrorView::dismiss);
  headerLayout->addWidget(dismissHeaderBtn);

  cardLayout->addWidget(headerWidget);
  // Hairline under the header (replaces a bare-QWidget border-bottom rule that
  // cascaded to every descendant widget).
  cardLayout->addWidget(theme::ui::separator(Qt::Horizontal, card));

  // ── Body ───────────────────────────────────────────────────────────
  auto *bodyWidget = new QWidget(card);
  auto *bodyLayout = new QVBoxLayout(bodyWidget);
  bodyLayout->setContentsMargins(24, 24, 24, 20);
  bodyLayout->setSpacing(18);

  // Icon + text row
  auto *iconTextRow = new QWidget(bodyWidget);
  auto *iconTextLayout = new QHBoxLayout(iconTextRow);
  iconTextLayout->setContentsMargins(0, 0, 0, 0);
  iconTextLayout->setSpacing(16);

  errorIconLayout = new QVBoxLayout();
  errorIconLayout->setContentsMargins(0, 2, 0, 0);
  updateErrorIcon();
  iconTextLayout->addLayout(errorIconLayout);

  auto *textCol = new QWidget(iconTextRow);
  auto *textLayout = new QVBoxLayout(textCol);
  textLayout->setContentsMargins(0, 0, 0, 0);
  textLayout->setSpacing(0);

  titleLabel = new QLabel(textCol);
  auto titleFont = theme::ui_sans(16);
  titleFont.setWeight(QFont::DemiBold);
  titleLabel->setFont(titleFont);
  titleLabel->setForegroundRole(QPalette::WindowText);
  textLayout->addWidget(titleLabel);

  subLabel = new QLabel(textCol);
  subLabel->setFont(theme::ui_sans(12));
  subLabel->setForegroundRole(QPalette::Dark);
  subLabel->setWordWrap(true);
  subLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  textLayout->addSpacing(5);
  textLayout->addWidget(subLabel);

  iconTextLayout->addWidget(textCol);
  bodyLayout->addWidget(iconTextRow);

  // Server chip
  serverChipLabel = theme::ui::chip(QString(), bodyWidget);
  serverChipLabel->setTextFormat(Qt::PlainText);
  bodyLayout->addWidget(serverChipLabel);

  // Actions row
  auto *actionsRow = new QWidget(bodyWidget);
  auto *actionsLayout = new QHBoxLayout(actionsRow);
  actionsLayout->setContentsMargins(0, 0, 0, 0);
  actionsLayout->setSpacing(8);

  reconnectBtn = theme::ui::primary_button("Reconnect", actionsRow);
  connect(reconnectBtn, &QPushButton::clicked, this, &MidSessionErrorView::reconnect);
  actionsLayout->addWidget(reconnectBtn);

  dismissBtn = theme::ui::outline_pill_button("Dismiss", actionsRow);
  connect(dismissBtn, &QPushButton::clicked, this, &MidSessionErrorView::dismiss);
  actionsLayout->addWidget(dismissBtn, 0, Qt::AlignRight);

  bodyLayout->addWidget(actionsRow);

  cardLayout->addWidget(bodyWidget);
}

void
MidSessionErrorView::updateErrorIcon()
{
  // Delete old icon and clear the layout
  if (errorIcon)
    {
      errorIcon->deleteLater();
      errorIcon = nullptr;
    }

  while (QLayoutItem *item = errorIconLayout->takeAt(0))
    delete item;

  // Create new icon based on current kind
  ErrorIcon::Kind iconKind = ErrorIcon::Kind::Gone;
  switch (currentKind)
    {
    case Kind::Gone:
      iconKind = ErrorIcon::Kind::Gone;
      break;
    case Kind::Offline:
      iconKind = ErrorIcon::Kind::Offline;
      break;
    case Kind::Timeout:
      iconKind = ErrorIcon::Kind::Timeout;
      break;
    }

  errorIcon = new ErrorIcon(iconKind, 36);
  errorIconLayout->addWidget(errorIcon);
  errorIconLayout->addStretch();
}

void
MidSessionErrorView::setError(Kind kind)
{
  currentKind = kind;
  updateErrorIcon();

  struct Labels {
    const char *title;
    const char *sub;
  };

  Labels labels{"", ""};
  switch (kind)
    {
    case Kind::Gone:
      labels = {"Server disconnected", "The connection was lost unexpectedly"};
      break;
    case Kind::Offline:
      labels = {"Server went offline", "freewave lost contact with the backend"};
      break;
    case Kind::Timeout:
      labels = {"Server stopped responding", "Connection timed out during playback"};
      break;
    }

  titleLabel->setText(labels.title);

  QString subText = labels.sub;
  if (wasPlaying)
    subText += " Playback has been paused.";
  subLabel->setText(subText);
}

void
MidSessionErrorView::setServerLabel(const QString &label)
{
  serverLabel = label;

  // Format as "● <label>" with a styled dot
  QString chipText = QString::fromUtf8("● ") + label;
  serverChipLabel->setText(chipText);
}

void
MidSessionErrorView::setWasPlaying(bool playing)
{
  wasPlaying = playing;

  // Update sub text to include/remove "Playback has been paused." if we have a title set
  if (!titleLabel->text().isEmpty())
    {
      QString subText = titleLabel->text(); // grab the base sub from current kind
      // Re-fetch the base sub text
      struct Labels {
        const char *title;
        const char *sub;
      };

      Labels labels{"", ""};
      switch (currentKind)
        {
        case Kind::Gone:
          labels = {"Server disconnected", "The connection was lost unexpectedly"};
          break;
        case Kind::Offline:
          labels = {"Server went offline", "freewave lost contact with the backend"};
          break;
        case Kind::Timeout:
          labels = {"Server stopped responding", "Connection timed out during playback"};
          break;
        }

      subText = labels.sub;
      if (wasPlaying)
        subText += " Playback has been paused.";
      subLabel->setText(subText);
    }
}

void
MidSessionErrorView::paintEvent(QPaintEvent *)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  // Dark overlay scrim: rgb(10, 15, 24) at ~72% alpha (184/255)
  p.fillRect(rect(), QColor(10, 15, 24, 184));
}

void
MidSessionErrorView::resizeEvent(QResizeEvent *event)
{
  QWidget::resizeEvent(event);

  // Center the card
  if (card)
    {
      int cardHeight = card->sizeHint().height();
      int x = (width() - card->width()) / 2;
      int y = (height() - cardHeight) / 2;
      card->move(x, y);
    }
}
