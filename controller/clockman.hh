#ifndef CLOCKMAN_HH
#define CLOCKMAN_HH

#include <QElapsedTimer>
#include <QObject>

#include "controller/playbackstate.hh"

class QTimer;

// Single shared wall-clock interpolator for the playback position. Backends
// only report elapsed time on discrete events (state changes, seeks, ~1s
// polls), never every frame, so anything that wants a smoothly advancing
// "current position" (progress bars, time labels) has to interpolate locally
// between those events. Every such consumer reads ticks from this one
// instance instead of running its own timer, so they move in lockstep rather
// than drifting apart on independently-phased clocks.
class ClockMan : public QObject {
  Q_OBJECT
public:
  explicit ClockMan(QObject *parent = nullptr);

public slots:
  // Full resync from a backend playback_state_changed event.
  void sync(PlaybackState state);
  // Optimistic re-anchor for a locally-initiated seek, ahead of the backend's
  // confirmation, so interpolation continues from the requested position
  // instead of resuming from the stale pre-seek one until the next sync().
  void seek_to(qint64 pos_ms);

signals:
  void tick(qint64 elapsed_ms, qint64 total_ms);

private:
  void on_tick();

  QTimer *timer = nullptr;
  QElapsedTimer wall;
  qint64 anchor_ms = 0;
  qint64 total_ms = 0;
  bool playing = false;
};

#endif // CLOCKMAN_HH
