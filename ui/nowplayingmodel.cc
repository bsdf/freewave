#include "nowplayingmodel.hh"

#include <algorithm>

namespace {
// Exponential smoothing for the ambient envelopes: very slow on purpose, so the
// wash drifts with the music's arc over seconds rather than reacting to
// individual hits. Per ~16 ms model update; rise slightly faster than it falls.
// (attack ~0.8 s, release ~1.7 s time constants.)
constexpr qreal ENV_ATTACK = 0.02;
constexpr qreal ENV_RELEASE = 0.01;

auto
ema_toward(qreal cur, qreal target) -> qreal
{
  const qreal a = target > cur ? ENV_ATTACK : ENV_RELEASE;
  return cur + a * (target - cur);
}
} // namespace

NowPlayingModel::NowPlayingModel(QObject *parent)
  : QObject(parent)
{
}

auto
NowPlayingModel::set_track(const QString &title, const QString &artist) -> void
{
  if (title_ == title && artist_ == artist)
    return;
  title_ = title;
  artist_ = artist;
  emit trackChanged();
}

auto
NowPlayingModel::set_album(const QString &album, const QString &year) -> void
{
  if (album_ == album && year_ == year)
    return;
  album_ = album;
  year_ = year;
  emit albumChanged();
}

auto
NowPlayingModel::set_audio_format(const QString &fmt) -> void
{
  if (audio_format_ == fmt)
    return;
  audio_format_ = fmt;
  emit audioFormatChanged();
}

auto
NowPlayingModel::set_play_state(const QString &state) -> void
{
  if (play_state_ == state)
    return;
  play_state_ = state;
  emit playStateChanged();
}

auto
NowPlayingModel::set_busy_text(const QString &text) -> void
{
  if (busy_text_ == text)
    return;
  busy_text_ = text;
  emit busyTextChanged();
}

auto
NowPlayingModel::set_progress(qint64 elapsed_ms, qint64 total_ms) -> void
{
  progress_elapsed_ms_ = elapsed_ms;
  progress_total_ms_ = total_ms;

  // ClockMan ticks up to ~30×/s while playing, so always publish to keep the
  // ms-precise bar in sync even when the integer second is unchanged.
  position_ = int(elapsed_ms / 1000);
  duration_ = int(total_ms / 1000);
  emit progressChanged();
}

auto
NowPlayingModel::set_accent(const QColor &accent) -> void
{
  if (accent_ == accent)
    return;
  accent_ = accent;
  emit accentChanged();
}

auto
NowPlayingModel::set_cover_id(const QString &id) -> void
{
  if (cover_id_ == id)
    return;
  cover_id_ = id;
  emit coverChanged();
}

auto
NowPlayingModel::set_bands(const QList<qreal> &bands) -> void
{
  // Updates ~60×/s; skip the equality compare and just publish.
  bands_ = bands;
  emit bandsChanged();

  // Slow bass envelope from the lowest few bands (≈ the low end after the log
  // mapping). Feeds ambient visualizations that breathe on the low end.
  const int n = std::min(4, static_cast<int>(bands.size()));
  qreal bass = 0.0;
  for (int i = 0; i < n; ++i)
    bass += bands[i];
  if (n > 0)
    bass /= n;
  wash_bass_ = ema_toward(wash_bass_, bass);
  emit washBassChanged();
}

auto
NowPlayingModel::set_level(qreal level) -> void
{
  level_ = level;
  emit levelChanged();

  wash_energy_ = ema_toward(wash_energy_, level);
  emit washEnergyChanged();
}

auto
NowPlayingModel::set_band_count(int n) -> void
{
  if (band_count_ == n)
    return;
  band_count_ = n;
  emit bandCountChanged();
}

auto
NowPlayingModel::set_history_rows(int n) -> void
{
  if (history_rows_ == n)
    return;
  history_rows_ = n;
  emit historyRowsChanged();
}

auto
NowPlayingModel::set_history(int revision, int head, qreal frac) -> void
{
  // frac advances ~60×/s; head and revision only when a row is committed.
  viz_frac_ = frac;
  emit vizFracChanged();
  if (viz_head_ != head)
    {
      viz_head_ = head;
      emit vizHeadChanged();
    }
  if (history_rev_ != revision)
    {
      history_rev_ = revision;
      emit historyRevChanged();
    }
}

auto
NowPlayingModel::set_visualizer_enabled(bool value) -> void
{
  if (visualizer_enabled_ == value)
    return;
  visualizer_enabled_ = value;
  emit visualizerEnabledChanged();
}

auto
NowPlayingModel::set_viz_preset(const QString &id) -> void
{
  if (viz_preset_ == id)
    return;
  viz_preset_ = id;
  emit vizPresetChanged();
}

auto
NowPlayingModel::set_fullscreen(bool value) -> void
{
  if (fullscreen_ == value)
    return;
  fullscreen_ = value;
  emit fullscreenChanged();
}
