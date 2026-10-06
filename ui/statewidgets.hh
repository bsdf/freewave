#ifndef STATEWIDGETS_HH
#define STATEWIDGETS_HH
#include <QWidget>
#include <QTimer>

// ─── PulseDot ─────────────────────────────────────────────────────────────────
// A small filled circle that pulses opacity (1.0→0.4) and scale (1.0→0.7)
// on a sine wave over ~1.4 seconds.
class PulseDot : public QWidget {
  Q_OBJECT

public:
  explicit PulseDot(int size = 8, QWidget *parent = nullptr);

protected:
  void paintEvent(QPaintEvent *) override;
  QSize sizeHint() const override;

private:
  int sz;
  QTimer timer;
  double phase = 0.0;
};

// ─── CountdownRing ────────────────────────────────────────────────────────────
// Draws a hairline background circle + an accent arc representing seconds/total,
// with the integer seconds drawn centered.
class CountdownRing : public QWidget {
  Q_OBJECT

public:
  explicit CountdownRing(int size = 44, QWidget *parent = nullptr);
  void setSeconds(int s);
  void setTotal(int t);

protected:
  void paintEvent(QPaintEvent *) override;
  QSize sizeHint() const override;

private:
  int sz;
  int seconds = 0;
  int total = 30;
};

// ─── ErrorIcon ────────────────────────────────────────────────────────────────
// Paints one of five lucide-style stroke glyphs:
// Offline (wifi with slash), Timeout (clock), Auth (lock),
// Partial (database), Gone (server-off).
class ErrorIcon : public QWidget {
  Q_OBJECT

public:
  enum class Kind { Offline,
    Timeout,
    Auth,
    Partial,
    Gone };

  explicit ErrorIcon(Kind kind, int size = 40, QWidget *parent = nullptr);

protected:
  void paintEvent(QPaintEvent *) override;
  QSize sizeHint() const override;

private:
  Kind kind;
  int sz;
};

#endif // STATEWIDGETS_HH
