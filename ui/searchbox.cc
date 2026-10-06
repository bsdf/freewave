#include "searchbox.hh"
#include "theme.hh"

#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>

// Field locks to one of two widths depending on focus (rather than stretching),
// so the layout around it doesn't reflow as the user types.
constexpr auto COLLAPSED_WIDTH = 200;
constexpr auto EXPANDED_WIDTH = 280;

SearchBox::SearchBox(QWidget *parent)
  : QLineEdit{parent}
{
  setFont(theme::ui_sans(13));

  // Don't grab focus on startup (or via Tab) — it would show its focus glow, even
  // behind the connecting overlay. Ctrl+K and clicks still focus it (setFocus
  // works regardless of ClickFocus).
  setFocusPolicy(Qt::ClickFocus);

  // Leading magnifier icon inside the field.
  addAction(QIcon(":/icons/search.svg"), QLineEdit::LeadingPosition);

  // Shortcut-hint badge pinned to the field's right edge; hidden once the field
  // is focused or contains text (see the focus handlers / textChanged).
  kbd_hint = new QLabel(
      QKeySequence(Qt::CTRL | Qt::Key_K).toString(QKeySequence::NativeText), this);
  kbd_hint->setAttribute(Qt::WA_TransparentForMouseEvents);
  kbd_hint->setFont(theme::mono(9));
  kbd_hint->setStyleSheet(
      "color: palette(mid); border: 1px solid palette(midlight);"
      " border-radius: 3px; padding: 1px 5px; background: palette(base);");
  auto *hint_layout = new QHBoxLayout(this);
  hint_layout->setContentsMargins(0, 6, 8, 6);
  hint_layout->addStretch(1);
  hint_layout->addWidget(kbd_hint);
  setTextMargins(0, 0, kbd_hint->sizeHint().width() + 6, 0);

  connect(this, &QLineEdit::textChanged, this, [this](const QString &t) {
    kbd_hint->setVisible(t.isEmpty() && !hasFocus());
  });
}

auto
SearchBox::keyPressEvent(QKeyEvent *event) -> void
{
  if (event->key() == Qt::Key_Escape)
    {
      clear();
      emit escaped();
      return;
    }
  QLineEdit::keyPressEvent(event);
}

auto
SearchBox::focusInEvent(QFocusEvent *event) -> void
{
  // Expand the field and tuck away the shortcut hint while typing.
  setMinimumWidth(EXPANDED_WIDTH);
  setMaximumWidth(EXPANDED_WIDTH);
  kbd_hint->hide();
  QLineEdit::focusInEvent(event);
}

auto
SearchBox::focusOutEvent(QFocusEvent *event) -> void
{
  setMinimumWidth(COLLAPSED_WIDTH);
  setMaximumWidth(COLLAPSED_WIDTH);
  if (text().isEmpty())
    kbd_hint->show();
  QLineEdit::focusOutEvent(event);
}
