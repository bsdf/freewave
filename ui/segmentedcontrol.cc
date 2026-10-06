#include "segmentedcontrol.hh"
#include "theme.hh"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QPushButton>

SegmentedControl::SegmentedControl(QWidget *parent)
  : QWidget{parent}
  , row(new QHBoxLayout(this))
  , group(new QButtonGroup(this))
{
  // A QWidget subclass only paints its stylesheet border/background when told to.
  setAttribute(Qt::WA_StyledBackground, true);
  setFont(theme::ui_sans(12));

  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(0);

  setStyleSheet(
      "SegmentedControl { border: 1px solid palette(midlight); border-radius: 4px; }"
      "QPushButton { border: none; border-radius: 0; padding: 5px 12px; background: transparent; }"
      "QPushButton:checked { background: palette(highlight); color: palette(highlighted-text); border-radius: 3px; }"
      "QPushButton:hover:!checked { background: palette(link); }");

  group->setExclusive(true);
  connect(group, &QButtonGroup::idClicked, this, &SegmentedControl::selected);
}

auto
SegmentedControl::add_segment(int id, const QString &label) -> QPushButton *
{
  auto *btn = new QPushButton(label, this);
  btn->setCheckable(true);
  group->addButton(btn, id);
  row->addWidget(btn);
  return btn;
}

auto
SegmentedControl::set_current(int id) -> void
{
  if (auto *btn = group->button(id))
    btn->setChecked(true);
}

auto
SegmentedControl::current() const -> int
{
  return group->checkedId();
}
