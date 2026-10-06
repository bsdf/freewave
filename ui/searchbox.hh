#ifndef SEARCHBOX_HH
#define SEARCHBOX_HH

#include <QLineEdit>

class QLabel;

// Library-header search field. Owns its magnifier icon, the Ctrl+K shortcut-hint
// badge, and its focus/escape interaction, so the host window doesn't have to
// filter its events. Emits escaped() on Escape (the field is cleared first) — the
// one concern it can't own itself, since where focus should land is the host's call.
class SearchBox : public QLineEdit {
  Q_OBJECT

public:
  explicit SearchBox(QWidget *parent = nullptr);

signals:
  void escaped();

protected:
  void keyPressEvent(QKeyEvent *event) override;
  void focusInEvent(QFocusEvent *event) override;
  void focusOutEvent(QFocusEvent *event) override;

private:
  QLabel *kbd_hint = nullptr;
};
#endif // SEARCHBOX_HH
