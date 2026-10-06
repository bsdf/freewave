#include "stalelibrarybanner.hh"
#include "theme.hh"
#include "ui/theme/components.hh"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QWidget>

// ── AlertIcon ──────────────────────────────────────────────────────────────
// Simple painted alert-circle widget
class AlertIcon : public QWidget {
public:
  explicit AlertIcon(QWidget *parent = nullptr)
    : QWidget(parent)
  {
    setFixedSize(14, 14);
  }

protected:
  void paintEvent(QPaintEvent *) override
  {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    auto accent = palette().color(QPalette::Highlight);
    p.setPen(QPen(accent, 1.5));
    p.setBrush(Qt::NoBrush);

    // Circle (center 7,7, radius 6)
    p.drawEllipse(QPointF(7, 7), 6, 6);

    // Exclamation mark: dot at bottom, line above
    p.setPen(QPen(accent, 1.5, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(7, 3), QPointF(7, 8));
    p.setPen(Qt::NoPen);
    p.setBrush(accent);
    p.drawEllipse(QPointF(7, 11), 0.8, 0.8);
  }
};

// ── StaleLibraryBanner ─────────────────────────────────────────────────────

StaleLibraryBanner::StaleLibraryBanner(QWidget *parent)
  : QWidget(parent)
{
  setFixedHeight(36);

  auto *layout = new QHBoxLayout(this);
  layout->setContentsMargins(16, 0, 16, 0);
  layout->setSpacing(10);

  // Alert icon
  auto *icon = new AlertIcon(this);
  layout->addWidget(icon, 0, Qt::AlignVCenter);

  // Message label
  message_label = new QLabel(this);
  message_label->setFont(theme::ui_sans(12));
  message_label->setTextFormat(Qt::RichText);
  message_label->setForegroundRole(QPalette::WindowText);
  layout->addWidget(message_label, 1, Qt::AlignVCenter);

  // Stretch
  layout->addStretch();

  // Retry button
  retry_btn = theme::ui::primary_button("RETRY", this);
  connect(retry_btn, &QPushButton::clicked, this, &StaleLibraryBanner::retry);
  layout->addWidget(retry_btn, 0, Qt::AlignVCenter);

  // Dismiss button
  dismiss_btn = new QPushButton("✕", this);
  dismiss_btn->setFont(theme::ui_sans(12));
  dismiss_btn->setStyleSheet(
      "QPushButton { background: transparent; color: palette(mid); "
      "border: none; padding: 2px; } "
      "QPushButton:hover { color: palette(dark); }");
  dismiss_btn->setFixedSize(20, 20);
  connect(dismiss_btn, &QPushButton::clicked, this, [this]() {
    hide();
    emit dismissed();
  });
  layout->addWidget(dismiss_btn, 0, Qt::AlignVCenter);

  setLayout(layout);
  rebuild_message();
}

void
StaleLibraryBanner::setAlbumCount(int n)
{
  album_count = n;
  rebuild_message();
}

void
StaleLibraryBanner::setLastSync(const QString &text)
{
  last_sync = text;
  rebuild_message();
}

void
StaleLibraryBanner::rebuild_message()
{
  QString msg = QString("Showing <b>%1 albums</b> from cache").arg(album_count);

  if (!last_sync.isEmpty())
    msg += QString(" — last synced %1").arg(last_sync);

  msg += ". <span style=\"color: palette(dark);\">Server is currently unreachable.</span>";

  message_label->setText(msg);
}

void
StaleLibraryBanner::paintEvent(QPaintEvent *evt)
{
  QPainter p(this);

  // Background: Highlight color at ~12% alpha
  auto accent = palette().color(QPalette::Highlight);
  accent.setAlphaF(0.12);
  p.fillRect(rect(), accent);

  // Bottom border: 1px full Highlight color
  auto accent_full = palette().color(QPalette::Highlight);
  p.setPen(QPen(accent_full, 1));
  p.drawLine(rect().bottomLeft(), rect().bottomRight());

  QWidget::paintEvent(evt);
}
