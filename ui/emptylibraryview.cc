#include "emptylibraryview.hh"
#include "theme.hh"
#include "ui/theme/components.hh"
#include "statewidgets.hh"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

EmptyLibraryView::EmptyLibraryView(QWidget *parent)
  : QWidget(parent)
{
  setAutoFillBackground(true);
  setBackgroundRole(QPalette::Window);

  auto *main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(0, 0, 0, 0);
  main_layout->setSpacing(0);
  main_layout->addStretch();

  // Card: Base surface, hairline border, centered icon + copy + refresh action.
  auto *card = theme::ui::card(this);
  auto *card_layout = new QVBoxLayout(card);
  card_layout->setContentsMargins(40, 36, 40, 36);
  card_layout->setSpacing(18);
  card_layout->setAlignment(Qt::AlignCenter);

  // Database glyph — same "empty/partial library" icon family as the error views.
  auto *icon = new ErrorIcon(ErrorIcon::Kind::Partial, 40, card);

  auto *title = new QLabel("Library is empty", card);
  auto title_font = theme::ui_sans(17);
  title_font.setWeight(QFont::Bold);
  title->setFont(title_font);
  title->setAlignment(Qt::AlignCenter);

  auto *sub = new QLabel("No albums found on this server.", card);
  sub->setFont(theme::ui_sans(12));
  sub->setForegroundRole(QPalette::Dark);
  sub->setAlignment(Qt::AlignCenter);

  auto *refresh_btn = theme::ui::primary_button("REFRESH", card);
  connect(refresh_btn, &QPushButton::clicked, this, &EmptyLibraryView::refresh);

  card_layout->addWidget(icon, 0, Qt::AlignCenter);
  card_layout->addWidget(title);
  card_layout->addWidget(sub);
  card_layout->addWidget(refresh_btn, 0, Qt::AlignCenter);

  // Help hint below the card.
  auto *hint = new QLabel(this);
  hint->setFont(theme::ui_sans(11));
  hint->setForegroundRole(QPalette::Mid);
  hint->setAlignment(Qt::AlignCenter);
  hint->setWordWrap(true);
  hint->setText(
      "If you just added music, rescan the server "
      "then refresh.");

  // Center the card + hint column horizontally.
  auto *center = new QWidget(this);
  auto *center_col = new QVBoxLayout(center);
  center_col->setContentsMargins(0, 0, 0, 0);
  center_col->setSpacing(20);
  center_col->setAlignment(Qt::AlignCenter);
  center_col->addWidget(card, 0, Qt::AlignCenter);
  center_col->addWidget(hint, 0, Qt::AlignCenter);

  auto *center_row = new QHBoxLayout();
  center_row->setContentsMargins(0, 0, 0, 0);
  center_row->addStretch();
  center_row->addWidget(center, 0);
  center_row->addStretch();

  main_layout->addLayout(center_row, 0);
  main_layout->addStretch();
}
