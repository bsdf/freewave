#ifndef TOGGLE_HH
#define TOGGLE_HH

#include <QWidget>

class QPropertyAnimation;

// Pill-shaped toggle: 36x20px, animated thumb.
class Toggle : public QWidget {
  Q_OBJECT
  Q_PROPERTY(int thumb_x READ thumb_x WRITE set_thumb_x)
public:
  explicit Toggle(QWidget *parent = nullptr);
  auto isChecked() const -> bool { return checked; }
  auto setChecked(bool v) -> void;
  auto thumb_x() const -> int { return tx; }
  auto set_thumb_x(int x) -> void
  {
    tx = x;
    update();
  }

signals:
  void toggled(bool value);

protected:
  void paintEvent(QPaintEvent *) override;
  void mousePressEvent(QMouseEvent *) override;

private:
  bool checked = false;
  int tx = 2;
  QPropertyAnimation *anim;
};
#endif // TOGGLE_HH
