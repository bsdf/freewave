#include "statewidgets.hh"
#include "anim.hh"
#include "theme.hh"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

// ─── PulseDot ─────────────────────────────────────────────────────────────────

PulseDot::PulseDot(int size, QWidget *parent)
  : QWidget(parent)
  , sz(size)
{
  setFixedSize(size, size);
  constexpr int tick_ms = 40;
  constexpr double cycle_s = 1.4;
  connect(&timer, &QTimer::timeout, this, [this]() {
    anim::advance_phase(phase, cycle_s, tick_ms);
    update();
  });
  timer.start(tick_ms);
}

void
PulseDot::paintEvent(QPaintEvent *)
{
  // Sine curve: 0→π gives 0.4→1.0→0.4 opacity and scale
  double t = std::sin(phase);     // -1 to 1
  double opacity = 0.7 + 0.3 * t; // Maps [-1,1] to [0.4, 1.0]
  double scale = 0.85 + 0.15 * t; // Maps [-1,1] to [0.7, 1.0]

  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  // Translate to center, scale, translate back
  int cx = sz / 2;
  int cy = sz / 2;
  p.translate(cx, cy);
  p.scale(scale, scale);
  p.translate(-cx, -cy);

  QColor color = palette().color(QPalette::Highlight);
  color.setAlphaF(opacity);
  p.setBrush(color);
  p.setPen(Qt::NoPen);

  int radius = sz / 2;
  p.drawEllipse(cx - radius, cy - radius, sz, sz);
}

QSize
PulseDot::sizeHint() const
{
  return QSize(sz, sz);
}

// ─── CountdownRing ────────────────────────────────────────────────────────────

CountdownRing::CountdownRing(int size, QWidget *parent)
  : QWidget(parent)
  , sz(size)
{
  setFixedSize(size, size);
}

void
CountdownRing::setSeconds(int s)
{
  if (seconds != s)
    {
      seconds = s;
      update();
    }
}

void
CountdownRing::setTotal(int t)
{
  if (total != t)
    {
      total = t;
      update();
    }
}

void
CountdownRing::paintEvent(QPaintEvent *)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  const double r = (sz - 4) / 2.0;
  const double cx = sz / 2.0;
  const double cy = sz / 2.0;

  // Background circle (hair color)
  QPen bg_pen(palette().color(QPalette::Midlight), 2);
  bg_pen.setCapStyle(Qt::RoundCap);
  bg_pen.setJoinStyle(Qt::RoundJoin);
  p.setPen(bg_pen);
  p.setBrush(Qt::NoBrush);
  p.drawEllipse(QPointF(cx, cy), r, r);

  // Progress arc (accent color)
  double pct = (total > 0) ? double(seconds) / double(total) : 0.0;
  double arc_span = 360.0 * pct;

  p.save();
  p.translate(cx, cy);
  p.rotate(-90); // Start at top (12 o'clock)

  QPen arc_pen(palette().color(QPalette::Highlight), 2);
  arc_pen.setCapStyle(Qt::RoundCap);
  arc_pen.setJoinStyle(Qt::RoundJoin);
  p.setPen(arc_pen);

  // Draw arc from top, spanning arc_span degrees clockwise
  QPainterPath arc_path;
  arc_path.arcMoveTo(-r, -r, 2 * r, 2 * r, 0);
  arc_path.arcTo(-r, -r, 2 * r, 2 * r, 0, arc_span);
  p.drawPath(arc_path);

  p.restore();

  // Draw the centered seconds text
  p.setFont(theme::mono(11));
  p.setPen(palette().color(QPalette::Dark));
  QString text = QString::number(seconds);
  QFontMetrics fm(p.font());
  int text_width = fm.horizontalAdvance(text);
  int text_height = fm.height();
  p.drawText(cx - text_width / 2, cy + text_height / 4, text);
}

QSize
CountdownRing::sizeHint() const
{
  return QSize(sz, sz);
}

// ─── ErrorIcon ────────────────────────────────────────────────────────────────

ErrorIcon::ErrorIcon(Kind kind, int size, QWidget *parent)
  : QWidget(parent)
  , kind(kind)
  , sz(size)
{
  setFixedSize(size, size);
}

void
ErrorIcon::paintEvent(QPaintEvent *)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  // Scale factor: viewBox is 0 0 24 24, so scale to fit sz
  double scale = sz / 24.0;
  p.translate(sz / 2.0, sz / 2.0);
  p.scale(scale, scale);
  p.translate(-12.0, -12.0);

  QColor stroke_color = palette().color(QPalette::Mid);
  QPen pen(stroke_color, 1.5);
  pen.setCapStyle(Qt::RoundCap);
  pen.setJoinStyle(Qt::RoundJoin);
  p.setPen(pen);
  p.setBrush(Qt::NoBrush);

  switch (kind)
    {
    case Kind::Offline:
      {
        // WiFi with slash line
        // Slash: (1,1) to (23,23)
        p.drawLine(QPointF(1, 1), QPointF(23, 23));

        // WiFi arcs (approximation)
        // Inner arc (5-12)
        p.drawArc(QRectF(5, 4, 14, 14), 0, 180 * 16);

        // Middle arc (3-14)
        p.drawArc(QRectF(3, 2, 18, 18), 0, 180 * 16);

        // Bottom dot
        p.setBrush(stroke_color);
        p.drawEllipse(QPointF(12, 20), 1, 1);
        break;
      }

    case Kind::Timeout:
      {
        // Clock
        p.drawEllipse(QPointF(12, 12), 10, 10);

        // Hour hand: 12 to 12 (center top)
        p.drawLine(QPointF(12, 12), QPointF(12, 6));

        // Minute hand: 12 to 3 (center right, longer)
        p.drawLine(QPointF(12, 12), QPointF(16, 14));

        // Diagonal slash accent (optional)
        p.drawLine(QPointF(2, 2), QPointF(5, 5));
        break;
      }

    case Kind::Auth:
      {
        // Lock
        // Body: rounded rect
        p.drawRect(QRectF(3, 11, 18, 11));
        p.drawRoundedRect(QRectF(3, 11, 18, 11), 2, 2);

        // Shackle: arc from top-left to top-right
        p.drawArc(QRectF(7, 5, 10, 10), 0, 180 * 16);

        // Keyhole dot
        p.setBrush(stroke_color);
        p.drawEllipse(QPointF(12, 16), 1, 1);
        break;
      }

    case Kind::Partial:
      {
        // Database (cylinder) with warning indicator
        // Top ellipse
        p.drawEllipse(QPointF(12, 5), 9, 3);

        // Body: vertical lines and bottom ellipse
        p.drawLine(QPointF(3, 5), QPointF(3, 19));
        p.drawLine(QPointF(21, 5), QPointF(21, 19));

        // Middle ellipse
        p.drawEllipse(QPointF(12, 12), 9, 3);

        // Bottom ellipse
        p.drawEllipse(QPointF(12, 19), 9, 3);

        // Warning line and dot
        p.drawLine(QPointF(12, 15), QPointF(12, 19));
        p.setBrush(stroke_color);
        p.drawEllipse(QPointF(12, 19), 1, 1);
        break;
      }

    case Kind::Gone:
      {
        // Server-off: two rectangles with a slash
        // Top server box
        p.drawRoundedRect(QRectF(2, 2, 20, 8), 2, 2);

        // Bottom server box
        p.drawRoundedRect(QRectF(2, 14, 20, 8), 2, 2);

        // Indicator dots
        p.setBrush(stroke_color);
        p.drawEllipse(QPointF(6, 6), 1, 1);
        p.drawEllipse(QPointF(6, 18), 1, 1);

        // Slash line
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(1, 1), QPointF(23, 23));
        break;
      }
    }
}

QSize
ErrorIcon::sizeHint() const
{
  return QSize(sz, sz);
}
