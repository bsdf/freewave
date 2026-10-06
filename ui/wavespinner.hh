#ifndef WAVESPINNER_HH
#define WAVESPINNER_HH

#include <QSize>
#include <QTimer>
#include <QWidget>

// Animated sine-wave loading indicator. The wave wiggles in place at 20 fps.
// Fixed to the size passed at construction.
class WaveSpinner : public QWidget {
public:
  explicit WaveSpinner(int w = 140, int h = 20, QWidget *parent = nullptr);

  QSize sizeHint() const override { return QSize(sz_w, sz_h); }
  QSize minimumSizeHint() const override { return sizeHint(); }

protected:
  void paintEvent(QPaintEvent *) override;

private:
  int sz_w, sz_h;
  QTimer timer;
  double phase = 0.0;
};

#endif /* WAVESPINNER_HH */
