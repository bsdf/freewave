#include "connectionfailedview.hh"
#include "theme.hh"
#include "ui/theme/components.hh"
#include "statewidgets.hh"
#include "fwmark.hh"

#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include <algorithm>

ConnectionFailedView::ConnectionFailedView(QWidget *parent)
  : QWidget(parent)
{
  setAutoFillBackground(true);
  setBackgroundRole(QPalette::Window);

  // Main vertical layout — centered content
  auto *main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(0, 0, 0, 0);
  main_layout->setSpacing(0);
  main_layout->addStretch();

  // Content column (centered, ~380px wide)
  auto *content_col = new QWidget(this);
  auto *content_layout = new QVBoxLayout(content_col);
  content_layout->setContentsMargins(0, 0, 0, 0);
  content_layout->setSpacing(28);
  content_layout->setAlignment(Qt::AlignCenter);

  // ─── Logo row (dim FWMark + "freewave" text) ───────────────────────────
  auto *logo_row = new QWidget(content_col);
  auto *logo_rl = new QHBoxLayout(logo_row);
  logo_rl->setContentsMargins(0, 0, 0, 0);
  logo_rl->setSpacing(10);
  logo_rl->setAlignment(Qt::AlignCenter);

  auto *logo_mark = new FWMark(18, logo_row);
  auto *logo_text = new QLabel("freewave", logo_row);
  auto logo_font = theme::ui_sans(13);
  logo_font.setWeight(QFont::Medium);
  logo_text->setFont(logo_font);
  logo_text->setForegroundRole(QPalette::Dark);
  logo_rl->addWidget(logo_mark);
  logo_rl->addWidget(logo_text);
  // Qt widgets ignore the CSS `opacity` property — dim the row with a real
  // graphics effect instead.
  auto *logo_opacity = new QGraphicsOpacityEffect(logo_row);
  logo_opacity->setOpacity(0.4);
  logo_row->setGraphicsEffect(logo_opacity);

  content_layout->addWidget(logo_row, 0, Qt::AlignCenter);

  // ─── Card (QFrame with Base bg, radius 8, no border, padding ~28px) ──
  auto *card = theme::ui::card(content_col, false);

  card_layout = new QVBoxLayout(card);
  card_layout->setContentsMargins(28, 32, 28, 32);
  card_layout->setSpacing(20);
  card_layout->setAlignment(Qt::AlignCenter);

  // Error icon (will be created/replaced in updateErrorIcon)
  error_icon = new ErrorIcon(ErrorIcon::Kind::Offline, 40, card);

  // Title + sub labels (centered)
  title_label = new QLabel(card);
  auto title_font = theme::ui_sans(17);
  title_font.setWeight(QFont::Bold);
  title_label->setFont(title_font);
  title_label->setAlignment(Qt::AlignCenter);

  sub_label = new QLabel(card);
  sub_label->setFont(theme::ui_sans(12));
  sub_label->setForegroundRole(QPalette::Dark);
  sub_label->setAlignment(Qt::AlignCenter);
  sub_label->setWordWrap(true); // the library-failure sub is a full sentence

  // Detail chip: full-width label with Window bg, Midlight border, radius 4
  detail_chip = theme::ui::chip(QString(), card);

  // Actions row: Try Again (primary) + Settings (secondary)
  auto *actions_row = new QWidget(card);
  auto *actions_rl = new QHBoxLayout(actions_row);
  actions_rl->setContentsMargins(0, 0, 0, 0);
  actions_rl->setSpacing(8);

  auto *try_again_btn = theme::ui::primary_button("TRY AGAIN", actions_row);
  connect(try_again_btn, &QPushButton::clicked, this, &ConnectionFailedView::retry);

  auto *settings_btn = theme::ui::outline_pill_button("SETTINGS", actions_row);
  connect(settings_btn, &QPushButton::clicked, this, &ConnectionFailedView::openSettings);

  actions_rl->addWidget(try_again_btn, 1);
  actions_rl->addWidget(settings_btn);

  // Populate card layout
  card_layout->addWidget(error_icon, 0, Qt::AlignCenter);
  card_layout->addWidget(title_label);
  card_layout->addWidget(sub_label);
  card_layout->addWidget(detail_chip);
  card_layout->addWidget(actions_row);

  content_layout->addWidget(card);

  // ─── Help hint line ───────────────────────────────────────────────────────
  // Text is server- and kind-dependent (updateHelpHint): the "is MPD running?"
  // tip only makes sense for an unreachable MPD server, not for a Subsonic
  // server or an auth/library failure where the server clearly responded.
  help_hint = new QLabel(content_col);
  help_hint->setFont(theme::ui_sans(11));
  help_hint->setForegroundRole(QPalette::Mid);
  help_hint->setAlignment(Qt::AlignCenter);
  help_hint->setWordWrap(true);

  content_layout->addWidget(help_hint, 0, Qt::AlignCenter);

  // Center content_col on screen
  auto *center_container = new QWidget(this);
  auto *center_layout = new QHBoxLayout(center_container);
  center_layout->setContentsMargins(0, 0, 0, 0);
  center_layout->setSpacing(0);
  center_layout->addStretch();
  center_layout->addWidget(content_col, 0);
  center_layout->addStretch();

  main_layout->addWidget(center_container, 1);
  main_layout->addStretch();

  // Set initial error state
  setError(Kind::Offline, "");
  setServerLabel("MPD ～ localhost:6600");
}

void
ConnectionFailedView::setError(Kind kind, const QString &detail)
{
  current_kind = kind;
  error_detail = detail;
  updateErrorIcon(kind);
  updateLabels(kind);
  updateDetailChip();
  updateHelpHint();
}

void
ConnectionFailedView::setServerLabel(const QString &label)
{
  server_label = label;
  updateDetailChip();
  updateHelpHint();
}

void
ConnectionFailedView::updateErrorIcon(Kind kind)
{
  ErrorIcon::Kind icon_kind;
  switch (kind)
    {
    case Kind::Offline:
      icon_kind = ErrorIcon::Kind::Offline;
      break;
    case Kind::Timeout:
      icon_kind = ErrorIcon::Kind::Timeout;
      break;
    case Kind::Auth:
      icon_kind = ErrorIcon::Kind::Auth;
      break;
    case Kind::Library:
      icon_kind = ErrorIcon::Kind::Partial;
      break;
    default:
      icon_kind = ErrorIcon::Kind::Offline;
    }

  // Remove old icon from layout and delete it
  if (error_icon)
    {
      card_layout->removeWidget(error_icon);
      delete error_icon;
    }

  // Create and insert new icon at position 0 (top of card)
  error_icon = new ErrorIcon(icon_kind, 40, nullptr);
  card_layout->insertWidget(0, error_icon, 0, Qt::AlignCenter);
}

void
ConnectionFailedView::updateLabels(Kind kind)
{
  QString title, sub;
  switch (kind)
    {
    case Kind::Offline:
      title = "Can't reach server";
      sub = "Server is offline or unreachable";
      break;
    case Kind::Timeout:
      title = "Connection timed out";
      sub = "Server took too long to respond";
      break;
    case Kind::Auth:
      title = "Authentication failed";
      sub = "Check your credentials in settings";
      break;
    case Kind::Library:
      title = "Couldn't load your library";
      sub = "Connected, but the library request failed — check your credentials in settings";
      break;
    }
  title_label->setText(title);
  sub_label->setText(sub);
  updateWrappedLabelHeights();
}

void
ConnectionFailedView::updateDetailChip()
{
  if (server_label.isEmpty())
    {
      detail_chip->setText("");
    }
  else if (error_detail.isEmpty())
    {
      detail_chip->setText(server_label);
    }
  else
    {
      detail_chip->setText(server_label + " ～ " + error_detail);
    }
}

void
ConnectionFailedView::updateHelpHint()
{
  // Only useful when an MPD server is genuinely unreachable. For a Subsonic
  // server, or an auth/library failure (the server responded), it's misleading.
  const bool is_mpd = server_label.startsWith("MPD");
  const bool unreachable = current_kind == Kind::Offline || current_kind == Kind::Timeout;
  if (is_mpd && unreachable)
    help_hint->setText(
        "Make sure MPD is running: "
        "<span style=\"font-family: 'IBM Plex Mono', monospace; color: palette(dark);\">systemctl status mpd</span>");
  else
    help_hint->clear();
  help_hint->setVisible(!help_hint->text().isEmpty());
  updateWrappedLabelHeights();
}

void
ConnectionFailedView::updateWrappedLabelHeights()
{
  // QBoxLayout::sizeHint() sums each child's QLabel::sizeHint(), which for a
  // word-wrapped label ignores wrapping — it only accounts for wrapped height
  // via heightForWidth() during the later geometry pass, by which point the
  // widget's overall height is already fixed too small. Pin a minimum height
  // explicitly once the label's actual width is known.
  auto apply_wrapped_height = [](QLabel *label) {
    if (label->width() <= 0)
      return;
    // heightForWidth() returns -1 for empty text — there's no minimum to pin.
    const int h = label->heightForWidth(label->width());
    label->setMinimumHeight(std::max(h, 0));
  };
  apply_wrapped_height(sub_label);
  apply_wrapped_height(help_hint);
}

void
ConnectionFailedView::resizeEvent(QResizeEvent *event)
{
  QWidget::resizeEvent(event);
  updateWrappedLabelHeights();
}
