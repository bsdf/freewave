#ifndef FWMARK_HH
#define FWMARK_HH

#include <QPalette>
#include <QWidget>

class FWMark : public QWidget {
public:
  explicit FWMark(int height = 22, QWidget *parent = nullptr);

  // Draw only the wave (no FW cross stroke). Default keeps both.
  void set_cross(bool on);
  // Palette role the strokes are painted with (default Highlight/accent).
  void set_stroke_role(QPalette::ColorRole role);

protected:
  void paintEvent(QPaintEvent *) override;

private:
  int sz_h;
  bool draw_cross = true;
  QPalette::ColorRole stroke_role = QPalette::Highlight;
};

#endif // FWMARK_HH
