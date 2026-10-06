#ifndef PLAYBACKSTATE_HH
#define PLAYBACKSTATE_HH

#include <cstdint>
#include <QMetaType>

enum class PlayState { Stopped,
  Playing,
  Paused };

// Work the player has started but not finished. `state` says what it is trying
// to do; this says whether it has got there. A seek into a large remote track
// can take ten seconds or more, and without this the difference between a slow
// seek and a hung player is invisible.
enum class PlayBusy { None,
  Loading,   // opening a stream: no audio yet
  Seeking }; // moving within a stream that is already open

struct PlaybackState {
  PlayState state = PlayState::Stopped;
  PlayBusy busy = PlayBusy::None;
  uint32_t elapsed_ms = 0;
  uint32_t total_ms = 0;
  int volume = 100; // 0-100, -1 = unavailable
  bool repeat = false;
  bool shuffle = false;
  bool single = false;
  int queue_pos = -1; // current position in queue, -1 = none

  // Audio format of the currently-playing stream (0 = unknown / not reported).
  uint32_t sample_rate_hz = 0;
  uint8_t bits = 0; // PCM sample depth; 0 for lossy/DSD/unknown
  uint8_t channels = 0;
  uint32_t bitrate_kbps = 0;
};

Q_DECLARE_METATYPE(PlaybackState)

#endif // PLAYBACKSTATE_HH
