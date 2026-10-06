#ifndef DOTSLIDER_HH
#define DOTSLIDER_HH

#include <QSize>
#include <QSlider>

// A QSlider painted as a flat 3px groove with a highlight sub-page fill and a
// small round head dot — the same dot (and radius) the WaveSlider scrubber
// rides, so the volume control and the seek bar share one handle vocabulary.
// Clicking or dragging anywhere sets the value (there is no separate handle to
// grab). Keyboard step and the value/maximum API are inherited unchanged.
class DotSlider : public QSlider {
public:
  explicit DotSlider(Qt::Orientation orientation, QWidget *parent = nullptr);

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

protected:
  void paintEvent(QPaintEvent *) override;
  void mousePressEvent(QMouseEvent *) override;
  void mouseMoveEvent(QMouseEvent *) override;

private:
  void set_from_x(double x);
};

#endif /* DOTSLIDER_HH */
