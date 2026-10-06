#ifndef PLAYBACK_HH
#define PLAYBACK_HH

#include <QSize>
#include <QLabel>
#include <QTimer>
#include <QSlider>
#include <QWidget>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QPixmap>
#include <QString>

#include <memory>

#include "controller/playbackstate.hh"
#include "model/song.hh"
#include "waveslider.hh"
#include "ui/favoriteheart.hh"

class NowPlayingBadge;
class FavoritesManager;

class PlaybackView : public QWidget {
  Q_OBJECT
public:
  explicit PlaybackView(std::shared_ptr<FavoritesManager> favman = nullptr,
      QWidget *parent = nullptr);

public slots:
  void set_status(PlaybackState state);
  void set_current_song(const song &song);
  void set_album_art(const QPixmap &pixmap);
  void set_queue_active(bool active);
  // Fed by ClockMan's shared tick; ignored while `seeking` (see clock_tick's
  // body). total_ms is unused here (max_position already tracks it via
  // set_status), kept for a uniform ClockMan::tick signature.
  void clock_tick(qint64 elapsed_ms, qint64 total_ms);

  void playpause();
  // Toggle the currently-playing song's favorite (heart button + `L` shortcut).
  void toggle_current_favorite();

signals:
  void play();
  void pause();
  void stop();
  void prev();
  void next();
  void seek(uint64_t pos_ms); // seek position in milliseconds
  void update_volume(int vol);
  void repeat_state(bool repeat_enabled, bool single_enabled);
  void shuffle_state(bool enabled);
  void toggle_queue();
  void open_now_playing();

protected:
  void changeEvent(QEvent *event) override;

private slots:
  void update_fonts();

private:
  void create_widgets();
  void setup_connections();
  void update_clock(const PlaybackState &state);
  void update_clock(uint64_t pos);
  void update_timelabel();
  void set_busy(PlayBusy state);
  void update_playbackslider();
  void update_playstate(bool playing);

  void stop_clicked();
  void repeat_clicked();
  void update_heart_icon();
  // Heart visibility follows the backend's live Favorites capability, which a
  // server can withdraw mid-session, not just at connect.
  void update_heart_availability();

  void playbackslider_seek();
  void volumeslider_seek();

  bool playing = false;
  bool repeat = false;
  bool single = false;
  bool shuffle = false;
  // seeking: a seek has been emitted and we are ignoring backend position
  // samples until one reconciles with our predicted clock (or the fallback
  // timer fires). dragging: the user is holding the slider handle.
  bool seeking = false;
  bool dragging = false;
  // What the player is waiting on, as reported by the backend, and whether that
  // wait has lasted long enough to be worth saying out loud. A warm seek settles
  // in well under the delay, so the common case shows nothing at all.
  PlayBusy busy = PlayBusy::None;
  bool busy_visible = false;

  uint64_t clock_position = 0;
  uint64_t max_position = 0;

  NowPlayingBadge *badge = nullptr;

  std::shared_ptr<FavoritesManager> favman;
  QString current_uri;
  HeartPopAnimator hearts;

  QLabel *time_label = nullptr;
  QLabel *time_total_label = nullptr;
  WaveSlider *playback_slider = nullptr;
  QSlider *volume_slider = nullptr;

  QPushButton *playpause_button = nullptr;
  QPushButton *stop_button = nullptr; // hidden, never in a layout — parented to this
  QPushButton *prev_button = nullptr;
  QPushButton *next_button = nullptr;
  QPushButton *repeat_button = nullptr;
  QPushButton *shuffle_button = nullptr;
  QPushButton *queue_button = nullptr;
  QPushButton *heart_button = nullptr;
  QFrame *heart_divider = nullptr; // parented into the transport layout

  QTimer *seek_settle_timer = nullptr;
  QTimer *seek_debounce_timer = nullptr;
  QTimer *busy_delay_timer = nullptr;
};

#endif /* PLAYBACK_HH */
