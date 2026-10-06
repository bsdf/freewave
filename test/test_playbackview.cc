// Tests for PlaybackView: transport signal wiring, the shared-clock seeking
// state machine (regression coverage for the ClockMan seek-jumpiness fix,
// submodule a4f1d42), and favorite-heart wiring. No prior coverage existed —
// PlaybackView isn't in freewave_qt_tests' dependency graph until this file.

#include <gtest/gtest.h>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QTest>

#include <memory>

#include "controller/backend.hh"
#include "controller/favoritesmanager.hh"
#include "controller/playbackstate.hh"
#include "model/song.hh"
#include "timeutil.hh"
#include "ui/nowplayingbadge.hh"
#include "ui/playback.hh"

namespace {

QPushButton *
find_button_by_name(const QWidget *w, const QString &name)
{
  for (auto *btn : w->findChildren<QPushButton *>())
    if (btn->objectName() == name)
      return btn;
  return nullptr;
}

QPushButton *
find_button_by_text(const QWidget *w, const QString &text)
{
  for (auto *btn : w->findChildren<QPushButton *>())
    if (btn->text() == text)
      return btn;
  return nullptr;
}

QPushButton *
find_button_by_tooltip(const QWidget *w, const QString &tip)
{
  for (auto *btn : w->findChildren<QPushButton *>())
    if (btn->toolTip() == tip)
      return btn;
  return nullptr;
}

auto
make_state(PlayState ps, uint32_t elapsed_ms, uint32_t total_ms) -> PlaybackState
{
  PlaybackState s;
  s.state = ps;
  s.elapsed_ms = elapsed_ms;
  s.total_ms = total_ms;
  return s;
}

} // namespace

// ---------------------------------------------------------------------------
// Minimal stub backend for FavoritesManager wiring. Distinctly named from
// FavStubBackend (test_favoritesmanager.cc) / MockBackend (test_albumartmanager.cc)
// since all three land in the same freewave_qt_tests binary.
// ---------------------------------------------------------------------------

class PlaybackTestBackend : public Backend {
  Q_OBJECT
public:
  bool supports_favorites = true;
  int set_favorite_calls = 0;
  QString last_uri;
  bool last_fav = false;

  auto get_albums() -> QList<album> override { return {}; }
  void fetch_songs(const album &, std::function<void(const QList<song> &)> cb) override
  {
    if (cb) cb({});
  }
  bool supports(Feature f) const override
  {
    return f == Feature::Favorites && supports_favorites;
  }
  void set_favorite(const QString &uri, bool fav) override
  {
    set_favorite_calls++;
    last_uri = uri;
    last_fav = fav;
  }

public slots:
  bool connect_to_server() override { return true; }
  void disconnect_from_server() override {}
  void play() override {}
  void pause() override {}
  void stop() override {}
  void prev() override {}
  void next() override {}
  void seek(uint32_t) override {}
  void set_volume(int) override {}
  void play_pos(uint32_t) override {}
  void set_repeat(bool, bool) override {}
  void set_shuffle(bool) override {}
  void insert_queue(const QList<song> &, uint32_t) override {}
  void append_queue(const QList<song> &) override {}
  void replace_queue(const QList<song> &, uint32_t) override {}
  void remove_from_queue(const QList<QModelIndex> &) override {}
  void rearrange_queue(uint32_t, std::vector<uint32_t>) override {}
  void fetch_album_art(const QString &) override {}
};

// ---------------------------------------------------------------------------
// Construction / default state
// ---------------------------------------------------------------------------

TEST(PlaybackViewTest, Construct_NoCrash_DefaultState)
{
  PlaybackView view;
  EXPECT_NE(find_button_by_text(&view, "▶"), nullptr);
  EXPECT_EQ(find_button_by_tooltip(&view, "Love this track (L)")->isVisibleTo(&view), false);
}

TEST(PlaybackViewTest, SetCurrentSong_NoFavman_HeartStaysHidden)
{
  PlaybackView view; // no FavoritesManager
  view.set_current_song(song{"uri1", "Title", "Artist", 1, 1, 200000, "hash"});

  auto *heart = find_button_by_tooltip(&view, "Love this track (L)");
  ASSERT_NE(heart, nullptr);
  EXPECT_FALSE(heart->isVisibleTo(&view));
}

// ---------------------------------------------------------------------------
// Favorites wiring
// ---------------------------------------------------------------------------

class PlaybackViewFavoritesTest : public ::testing::Test {
protected:
  std::shared_ptr<PlaybackTestBackend> stub;
  std::shared_ptr<FavoritesManager> favman;
  std::unique_ptr<PlaybackView> view;

  void SetUp() override
  {
    stub = std::make_shared<PlaybackTestBackend>();
    favman = std::make_shared<FavoritesManager>(stub);
    view = std::make_unique<PlaybackView>(favman);
  }
};

TEST_F(PlaybackViewFavoritesTest, HeartVisibleWhenBackendSupportsFavorites)
{
  view->set_current_song(song{"s1", "T", "A", 1, 1, 1000, "h"});
  auto *heart = find_button_by_tooltip(view.get(), "Love this track (L)");
  ASSERT_NE(heart, nullptr);
  EXPECT_TRUE(heart->isVisibleTo(view.get()));
}

TEST_F(PlaybackViewFavoritesTest, HeartHiddenWhenBackendLacksFavorites)
{
  stub->supports_favorites = false;
  view->set_current_song(song{"s1", "T", "A", 1, 1, 1000, "h"});
  auto *heart = find_button_by_tooltip(view.get(), "Love this track (L)");
  ASSERT_NE(heart, nullptr);
  EXPECT_FALSE(heart->isVisibleTo(view.get()));
}

TEST_F(PlaybackViewFavoritesTest, ToggleCurrentFavorite_PersistsThroughBackend)
{
  view->set_current_song(song{"s1", "T", "A", 1, 1, 1000, "h"});
  view->toggle_current_favorite();

  EXPECT_EQ(stub->set_favorite_calls, 1);
  EXPECT_EQ(stub->last_uri, "s1");
  EXPECT_TRUE(stub->last_fav); // was not favorited, toggles on
}

TEST_F(PlaybackViewFavoritesTest, ToggleCurrentFavorite_NoCurrentSong_NoOp)
{
  view->toggle_current_favorite(); // current_uri is empty
  EXPECT_EQ(stub->set_favorite_calls, 0);
}

// ---------------------------------------------------------------------------
// Now-playing badge forwarding
// ---------------------------------------------------------------------------

TEST(PlaybackViewTest, SetCurrentSong_ForwardsToBadge)
{
  PlaybackView view;
  view.set_current_song(song{"uri1", "Title", "Artist", 1, 1, 200000, "hash"});

  auto *badge = view.findChild<NowPlayingBadge *>();
  ASSERT_NE(badge, nullptr);
  EXPECT_EQ(badge->toolTip(), "Title ～ Artist");
}

// ---------------------------------------------------------------------------
// Transport signals
// ---------------------------------------------------------------------------

TEST(PlaybackViewTest, Playpause_EmitsPlayThenPauseAsStateFlips)
{
  PlaybackView view;
  QSignalSpy play_spy(&view, &PlaybackView::play);
  QSignalSpy pause_spy(&view, &PlaybackView::pause);

  view.playpause();
  EXPECT_EQ(play_spy.count(), 1);
  EXPECT_EQ(pause_spy.count(), 0);

  view.set_status(make_state(PlayState::Playing, 1000, 60000));
  view.playpause();
  EXPECT_EQ(pause_spy.count(), 1);
}

TEST(PlaybackViewTest, SetQueueActive_TogglesQueueButtonChecked)
{
  PlaybackView view;
  auto *queue_btn = find_button_by_text(&view, "Queue");
  ASSERT_NE(queue_btn, nullptr);

  view.set_queue_active(true);
  EXPECT_TRUE(queue_btn->isChecked());

  view.set_queue_active(false);
  EXPECT_FALSE(queue_btn->isChecked());
}

TEST(PlaybackViewTest, RepeatClicked_CyclesThroughRepeatSingleStates)
{
  PlaybackView view;
  auto *repeat_btn = find_button_by_name(&view, "repeat_button");
  ASSERT_NE(repeat_btn, nullptr);

  QSignalSpy spy(&view, &PlaybackView::repeat_state);

  // off -> repeat all
  repeat_btn->click();
  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.at(0).at(0).toBool(), true);
  EXPECT_EQ(spy.at(0).at(1).toBool(), false);

  // repeat all -> repeat single
  view.set_status(make_state(PlayState::Playing, 0, 0)); // state.repeat/single default false
  auto s = make_state(PlayState::Playing, 0, 0);
  s.repeat = true;
  s.single = false;
  view.set_status(s);
  repeat_btn->click();
  ASSERT_EQ(spy.count(), 2);
  EXPECT_EQ(spy.at(1).at(0).toBool(), true);
  EXPECT_EQ(spy.at(1).at(1).toBool(), true);

  // repeat single -> off
  s.repeat = true;
  s.single = true;
  view.set_status(s);
  repeat_btn->click();
  ASSERT_EQ(spy.count(), 3);
  EXPECT_EQ(spy.at(2).at(0).toBool(), false);
  EXPECT_EQ(spy.at(2).at(1).toBool(), false);
}

TEST(PlaybackViewTest, PrevClicked_RestartsBelowThreshold_SkipsAboveThreshold)
{
  PlaybackView view;
  auto *prev_btn = find_button_by_name(&view, "prev_button");
  ASSERT_NE(prev_btn, nullptr);

  QSignalSpy prev_spy(&view, &PlaybackView::prev);
  QSignalSpy seek_spy(&view, &PlaybackView::seek);

  view.set_status(make_state(PlayState::Playing, 1000, 60000)); // below 5s threshold
  prev_btn->click();
  EXPECT_EQ(prev_spy.count(), 1);
  EXPECT_EQ(seek_spy.count(), 0);

  view.set_status(make_state(PlayState::Playing, 6000, 60000)); // above threshold
  prev_btn->click();
  EXPECT_EQ(prev_spy.count(), 1); // unchanged
  ASSERT_EQ(seek_spy.count(), 1);
  EXPECT_EQ(seek_spy.constFirst().at(0).toULongLong(), 0u);
}

// ---------------------------------------------------------------------------
// Shared clock (ClockMan) integration
// ---------------------------------------------------------------------------

TEST(PlaybackViewTest, SetStatus_UpdatesSliderRangeAndTimeLabel)
{
  PlaybackView view;
  auto *slider = view.findChild<QSlider *>("playback_slider");
  ASSERT_NE(slider, nullptr);
  auto *time_label = view.findChild<QLabel *>("time_label");
  ASSERT_NE(time_label, nullptr);

  view.set_status(make_state(PlayState::Playing, 5000, 60000));

  EXPECT_EQ(slider->maximum(), 60000);
  EXPECT_EQ(slider->sliderPosition(), 5000);
  EXPECT_EQ(time_label->text(), timeutil::ms_to_text(5000));
}

// A seek long enough to be mistaken for a hang has to announce itself; a quick
// one must not flicker the clock for two frames on the way past.
TEST(PlaybackViewTest, Busy_AnnouncesItselfOnlyOnceTheWaitIsLongEnough)
{
  PlaybackView view;
  auto *time_label = view.findChild<QLabel *>("time_label");
  ASSERT_NE(time_label, nullptr);

  view.set_status(make_state(PlayState::Playing, 5000, 60000));
  ASSERT_EQ(time_label->text(), timeutil::ms_to_text(5000));

  auto seeking = make_state(PlayState::Playing, 30000, 60000);
  seeking.busy = PlayBusy::Seeking;
  view.set_status(seeking);
  EXPECT_NE(time_label->text(), QString("seeking")) << "announced immediately";

  QTest::qWait(600);
  EXPECT_EQ(time_label->text(), QString("seeking"));

  auto settled = make_state(PlayState::Playing, 30000, 60000);
  view.set_status(settled);
  EXPECT_EQ(time_label->text(), timeutil::ms_to_text(30000));
}

// The elapsed time and the word that replaces it share one label, so the label
// has to be sized for the wider of the two up front — otherwise announcing a
// seek shoves the slider sideways mid-playback.
TEST(PlaybackViewTest, Busy_AnnouncementDoesNotMoveTheSlider)
{
  PlaybackView view;
  view.resize(900, 96);
  view.show();
  auto *slider = view.findChild<QSlider *>("playback_slider");
  ASSERT_NE(slider, nullptr);

  view.set_status(make_state(PlayState::Playing, 5000, 60000));
  QTest::qWait(50);
  const QRect before = slider->geometry();
  ASSERT_GT(before.x(), 0) << "layout never ran; the check would be vacuous";

  auto seeking = make_state(PlayState::Playing, 30000, 60000);
  seeking.busy = PlayBusy::Seeking;
  view.set_status(seeking);
  QTest::qWait(600);

  EXPECT_EQ(slider->geometry(), before);
}

TEST(PlaybackViewTest, Busy_ShortWaitNeverShows)
{
  PlaybackView view;
  auto *time_label = view.findChild<QLabel *>("time_label");

  view.set_status(make_state(PlayState::Playing, 5000, 60000));
  auto seeking = make_state(PlayState::Playing, 5000, 60000);
  seeking.busy = PlayBusy::Seeking;
  view.set_status(seeking);

  QTest::qWait(100);
  view.set_status(make_state(PlayState::Playing, 8000, 60000));
  QTest::qWait(600);

  EXPECT_EQ(time_label->text(), timeutil::ms_to_text(8000));
}

TEST(PlaybackViewTest, ClockTick_AdvancesPositionWhenNotSeeking)
{
  PlaybackView view;
  auto *slider = view.findChild<QSlider *>("playback_slider");
  auto *time_label = view.findChild<QLabel *>("time_label");

  view.set_status(make_state(PlayState::Playing, 1000, 60000));
  view.clock_tick(6000, 60000);

  EXPECT_EQ(slider->sliderPosition(), 6000);
  EXPECT_EQ(time_label->text(), timeutil::ms_to_text(6000));
}

// Regression coverage for the ClockMan seek-jumpiness fix (submodule a4f1d42):
// clock_tick must hard-ignore ClockMan re-anchors while a seek is settling,
// same as it already ignored them while the handle is being dragged.
TEST(PlaybackViewTest, SeekingGuard_ClockTickIgnoredWhileDragging)
{
  PlaybackView view;
  auto *slider = view.findChild<QSlider *>("playback_slider");
  auto *time_label = view.findChild<QLabel *>("time_label");

  view.set_status(make_state(PlayState::Playing, 1000, 60000));
  ASSERT_EQ(time_label->text(), timeutil::ms_to_text(1000));

  slider->setSliderDown(true); // press: arms dragging + seeking

  // A stale/late ClockMan tick arriving mid-drag must not move the clock.
  view.clock_tick(5000, 60000);
  EXPECT_EQ(time_label->text(), timeutil::ms_to_text(1000));
  EXPECT_EQ(slider->sliderPosition(), 1000);
}

TEST(PlaybackViewTest, SeekingGuard_IgnoresStaleBackendReportsUntilReconciled)
{
  PlaybackView view;
  auto *slider = view.findChild<QSlider *>("playback_slider");
  auto *time_label = view.findChild<QLabel *>("time_label");
  QSignalSpy seek_spy(&view, &PlaybackView::seek);

  view.set_status(make_state(PlayState::Playing, 1000, 60000));

  slider->setSliderDown(true);
  slider->setSliderPosition(30000); // simulate the user dragging the handle
  slider->setSliderDown(false);     // release: commits the seek

  ASSERT_EQ(seek_spy.count(), 1);
  EXPECT_EQ(seek_spy.constFirst().at(0).toULongLong(), 30000u);
  EXPECT_EQ(time_label->text(), timeutil::ms_to_text(30000));

  // The backend keeps reporting the pre-seek position for a tick or two while
  // the flush-seek settles — must be ignored (diff from clock_position is
  // outside the reconcile tolerance).
  view.set_status(make_state(PlayState::Playing, 1000, 60000));
  EXPECT_EQ(time_label->text(), timeutil::ms_to_text(30000))
      << "stale pre-seek backend position must not yank the clock backwards";
  EXPECT_EQ(slider->sliderPosition(), 30000);

  // A ClockMan tick still must not land either — seeking has not cleared.
  view.clock_tick(1050, 60000);
  EXPECT_EQ(time_label->text(), timeutil::ms_to_text(30000));

  // Once the backend position reconciles within tolerance, seeking clears...
  view.set_status(make_state(PlayState::Playing, 30500, 60000));
  EXPECT_EQ(time_label->text(), timeutil::ms_to_text(30500));

  // ...and subsequent ClockMan ticks are honored again.
  view.clock_tick(31000, 60000);
  EXPECT_EQ(time_label->text(), timeutil::ms_to_text(31000));
  EXPECT_EQ(slider->sliderPosition(), 31000);
}

#include "test_playbackview.moc"
