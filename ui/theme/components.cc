#include "ui/theme/components.hh"

#include <QColor>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QWidget>

#include "ui/theme.hh"

namespace theme::ui {

namespace {

// Canonical recipes. tok values are interpolated so radius/padding track the
// scale; colors stay as palette(role). letter-spacing/text-transform are
// intentionally omitted — Qt QSS ignores them, and the call sites already pass
// pre-cased text.
QString
primary_qss()
{
  return QString("QPushButton {"
                 " background: palette(highlight);"
                 " color: palette(highlighted-text);"
                 " border: none;"
                 " border-radius: %1px;"
                 " padding: 9px 16px; }"
                 "QPushButton:hover { background: palette(highlight); }")
      .arg(int(tok::rMd));
}

QString
outline_qss()
{
  return QString("QPushButton {"
                 " background: transparent;"
                 " color: palette(window-text);"
                 " border: 1px solid palette(midlight);"
                 " border-radius: %1px;"
                 " padding: 9px 16px; }"
                 "QPushButton:hover {"
                 " border-color: palette(highlight);"
                 " color: palette(highlight); }")
      .arg(int(tok::rMd));
}

QString
back_qss()
{
  // Subtle fill on hover (rather than the outline pill's border/text accent)
  // so the monochrome chevron icon and label stay visually in step.
  return QString("QPushButton {"
                 " background: palette(base);"
                 " color: palette(window-text);"
                 " border: 1px solid palette(midlight);"
                 " border-radius: %1px;"
                 " padding: 5px 10px 5px 8px;"
                 " text-align: left; }"
                 "QPushButton:hover {"
                 " background: palette(link); }")
      .arg(int(tok::rMd));
}

QString
chip_qss(bool compact)
{
  return QString("QLabel {"
                 " background: palette(window);"
                 " border: 1px solid palette(midlight);"
                 " border-radius: %1px;"
                 " padding: %2; }")
      .arg(int(compact ? tok::rSm : tok::rMd))
      .arg(compact ? "1px 5px" : "7px 12px");
}

QString
card_qss(bool bordered)
{
  return QString("QFrame {"
                 " background-color: palette(base);"
                 " border: %1;"
                 " border-radius: %2px; }")
      .arg(bordered ? "1px solid palette(midlight)" : "none")
      .arg(int(tok::rLg));
}

} // namespace

auto
style_as_primary_button(QPushButton *btn) -> void
{
  btn->setObjectName("fwPrimaryButton");
  auto font = theme::ui_sans(11);
  font.setWeight(QFont::Medium);
  btn->setFont(font);
  btn->setCursor(Qt::PointingHandCursor);
  btn->setStyleSheet(primary_qss());
}

auto
primary_button(const QString &text, QWidget *parent) -> QPushButton *
{
  auto *btn = new QPushButton(text, parent);
  style_as_primary_button(btn);
  return btn;
}

auto
style_as_outline_button(QPushButton *btn) -> void
{
  btn->setObjectName("fwOutlinePill");
  auto font = theme::ui_sans(11);
  font.setWeight(QFont::Medium);
  btn->setFont(font);
  btn->setCursor(Qt::PointingHandCursor);
  btn->setStyleSheet(outline_qss());
}

auto
outline_pill_button(const QString &text, QWidget *parent) -> QPushButton *
{
  auto *btn = new QPushButton(text, parent);
  style_as_outline_button(btn);
  return btn;
}

auto
style_as_back_button(QPushButton *btn, const QString &label) -> void
{
  btn->setObjectName("fwBackButton");
  // Uppercase + tracking set here (not QSS): Qt stylesheets support neither
  // text-transform nor reliably letter-spacing on QPushButton.
  btn->setText(label.toUpper());
  auto font = theme::mono(10);
  font.setWeight(QFont::Medium);
  font.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
  btn->setFont(font);
  btn->setIcon(QIcon(":/icons/chevron-left.svg"));
  btn->setIconSize(QSize(12, 12));
  btn->setCursor(Qt::PointingHandCursor);
  btn->setStyleSheet(back_qss());
}

auto
back_button(const QString &label, QWidget *parent) -> QPushButton *
{
  auto *btn = new QPushButton(parent);
  style_as_back_button(btn, label);
  return btn;
}

auto
chip(const QString &text, QWidget *parent, bool compact) -> QLabel *
{
  auto *label = new QLabel(text, parent);
  label->setObjectName("fwChip");
  label->setFont(compact ? theme::mono(9) : theme::type::mono_meta());
  label->setForegroundRole(QPalette::Dark);
  label->setAlignment(Qt::AlignCenter);
  label->setStyleSheet(chip_qss(compact));
  return label;
}

auto
card(QWidget *parent, bool bordered) -> QFrame *
{
  auto *frame = new QFrame(parent);
  frame->setObjectName("fwCard");
  frame->setFrameShape(QFrame::NoFrame);
  frame->setAutoFillBackground(true);
  frame->setBackgroundRole(QPalette::Base);
  frame->setStyleSheet(card_qss(bordered));
  return frame;
}

auto
section_header(const QString &text, QWidget *parent) -> QLabel *
{
  auto *label = new QLabel(text, parent);
  label->setObjectName("fwSectionHeader");
  label->setFont(theme::type::section_label());
  label->setForegroundRole(QPalette::PlaceholderText);
  return label;
}

auto
separator(Qt::Orientation orientation, QWidget *parent) -> QFrame *
{
  // Canonical 1px hairline: a plain Midlight-filled frame, fixed in its thin
  // dimension. (HLine/VLine frame shapes render thicker than 1px, so we use a
  // fixed-size NoFrame instead — this matches every hand-rolled hairline.)
  auto *line = new QFrame(parent);
  line->setObjectName("fwSeparator");
  line->setFrameShape(QFrame::NoFrame);
  line->setAutoFillBackground(true);
  line->setBackgroundRole(QPalette::Midlight);
  if (orientation == Qt::Horizontal)
    line->setFixedHeight(1);
  else
    line->setFixedWidth(1);
  return line;
}

auto
cover_shadow(QWidget *target, int cover_px) -> void
{
  auto *shadow = new QGraphicsDropShadowEffect(target);
  shadow->setBlurRadius(cover_px * 0.187);
  shadow->setOffset(0, cover_px * 0.08);
  shadow->setColor(QColor(0, 0, 0, 70));
  target->setGraphicsEffect(shadow);
}

} // namespace theme::ui
