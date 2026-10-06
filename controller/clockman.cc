#include "clockman.hh"

#include <QTimer>
#include <algorithm>

namespace {
// ~30 fps. Widgets render sub-pixel positions (WaveSlider's fill edge/head),
// so motion reads as smooth at this rate without the repaint cost of a
// higher one.
constexpr int TICK_MS = 33;
}

ClockMan::ClockMan(QObject *parent)
  : QObject(parent)
{
  timer = new QTimer(this);
  timer->setTimerType(Qt::PreciseTimer);
  timer->setInterval(TICK_MS);
  connect(timer, &QTimer::timeout, this, &ClockMan::on_tick);
}

auto
ClockMan::sync(PlaybackState state) -> void
{
  anchor_ms = state.elapsed_ms;
  total_ms = state.total_ms;
  playing = (state.state == PlayState::Playing);
  wall.restart();

  if (playing)
    timer->start();
  else
    timer->stop();

  emit tick(anchor_ms, total_ms);
}

auto
ClockMan::seek_to(qint64 pos_ms) -> void
{
  anchor_ms = std::clamp<qint64>(pos_ms, 0, total_ms);
  wall.restart();
  emit tick(anchor_ms, total_ms);
}

auto
ClockMan::on_tick() -> void
{
  const qint64 e = std::clamp<qint64>(anchor_ms + wall.elapsed(), 0, total_ms);
  emit tick(e, total_ms);
}
