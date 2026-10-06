#include "themebridge.hh"
#include "theme.hh"
#include "theme/tokens.hh"

ThemeBridge::ThemeBridge(QObject *parent)
  : QObject(parent)
{
}

auto
ThemeBridge::sans_family() const -> QString
{
  return theme::ui_sans().families().constFirst();
}

auto
ThemeBridge::mono_family() const -> QString
{
  return theme::mono().families().constFirst();
}

auto
ThemeBridge::wash_near() const -> QColor
{
  return theme::tok::npwash::tint_near;
}

auto
ThemeBridge::wash_mid() const -> QColor
{
  return theme::tok::npwash::tint_mid;
}

auto
ThemeBridge::wash_far() const -> QColor
{
  return theme::tok::npwash::tint_far;
}

auto
ThemeBridge::text_color() const -> QColor
{
  return theme::tok::npwash::text;
}

QColor
ThemeBridge::textAlpha(qreal a) const
{
  QColor c = theme::tok::npwash::text;
  c.setAlphaF(static_cast<float>(a));
  return c;
}
