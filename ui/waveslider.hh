#ifndef WAVESLIDER_HH
#define WAVESLIDER_HH

#include <QSize>
#include <QSlider>

// A QSlider that paints a sine wave instead of a groove+handle: the played
// portion is drawn in the accent color, the rest in the hairline color, with a
// small head dot riding the wave at the current position. Clicking or dragging
// anywhere on the track seeks to that absolute position (there is no handle to
// grab, so the default groove behaviour is replaced). Keyboard seek and the
// value/maximum API are inherited unchanged.
class WaveSlider : public QSlider {
public:
  explicit WaveSlider(Qt::Orientation orientation, QWidget *parent = nullptr);

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

protected:
  void paintEvent(QPaintEvent *) override;
  void mousePressEvent(QMouseEvent *) override;
  void mouseMoveEvent(QMouseEvent *) override;
  void mouseReleaseEvent(QMouseEvent *) override;

private:
  void seek_to_x(double x);
};

#endif /* WAVESLIDER_HH */
