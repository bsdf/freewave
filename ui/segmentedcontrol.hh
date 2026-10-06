#ifndef SEGMENTEDCONTROL_HH
#define SEGMENTEDCONTROL_HH

#include <QWidget>

class QButtonGroup;
class QHBoxLayout;
class QPushButton;

// A row of mutually-exclusive segments styled as a single bordered control (the
// selected segment fills with the highlight color). Add segments with stable ids;
// emits selected(int) when the user picks one. add_segment() returns the button so
// callers can tweak per-segment state (e.g. hide a segment for a disabled backend).
class SegmentedControl : public QWidget {
  Q_OBJECT

public:
  explicit SegmentedControl(QWidget *parent = nullptr);

  auto add_segment(int id, const QString &label) -> QPushButton *;
  auto set_current(int id) -> void;
  auto current() const -> int;

signals:
  void selected(int id);

private:
  QHBoxLayout *row;
  QButtonGroup *group;
};
#endif // SEGMENTEDCONTROL_HH
