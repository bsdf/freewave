#ifndef MOCK_AUDIO_ENGINE_HH
#define MOCK_AUDIO_ENGINE_HH

#include "controller/audioengine.hh"

// AUTOMOC won't pick up a header-only mock. Include the generated moc file
// directly at the bottom of exactly one .cc translation unit that uses this.
// Usage: at the end of the test .cc file add:
//   #include "moc_mock_audio_engine.cpp"   (handled by AUTOMOC automatically)

// Controllable AudioEngine stub for unit tests.
class MockAudioEngine : public AudioEngine {
  Q_OBJECT
public:
  explicit MockAudioEngine(QObject *parent = nullptr)
    : AudioEngine{parent}
  {
  }

  void play(const QUrl &url, uint32_t start_ms = 0) override
  {
    last_play_url = url;
    last_play_start_ms = start_ms;
    play_count++;
    state_ = State::Playing;
    emit state_changed(state_);
  }
  void set_next_url(const QUrl &url) override { next_url = url; }
  void clear_next_url() override { next_url.clear(); }
  void pause() override
  {
    state_ = State::Paused;
    emit state_changed(state_);
  }
  void resume() override
  {
    state_ = State::Playing;
    emit state_changed(state_);
  }
  void stop() override
  {
    state_ = State::Stopped;
    emit state_changed(state_);
  }
  void seek(uint32_t ms) override
  {
    last_seek_ms = ms;
    seek_count++;
  }
  void set_volume(int v) override { vol = v; }

  State state() const override { return state_; }
  Busy busy() const override { return busy_; }
  uint32_t position_ms() const override { return pos; }
  uint32_t duration_ms() const override { return dur; }
  int volume() const override { return vol; }

  // Simulate engine events from tests
  void fire_track_finished() { emit track_finished(); }
  // EOS as the real engine reports it: Stopped is recorded, not announced.
  void fire_eos()
  {
    state_ = State::Stopped;
    emit track_finished();
  }
  void fire_track_changed(const QUrl &uri = {}) { emit track_changed(uri); }
  void fire_busy(Busy b)
  {
    busy_ = b;
    emit busy_changed(b);
  }

  // Inspection
  QUrl last_play_url;
  QUrl next_url;
  uint32_t last_play_start_ms = 0;
  uint32_t last_seek_ms = 0;
  int play_count = 0;
  int seek_count = 0;

  void set_position(uint32_t ms) { pos = ms; }
  void set_duration(uint32_t ms) { dur = ms; }

private:
  State state_ = State::Stopped;
  Busy busy_ = Busy::None;
  uint32_t pos = 0;
  uint32_t dur = 0;
  int vol = 100;
};

#endif // MOCK_AUDIO_ENGINE_HH
