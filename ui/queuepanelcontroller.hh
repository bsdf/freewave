#ifndef QUEUEPANELCONTROLLER_HH
#define QUEUEPANELCONTROLLER_HH

#include <QObject>

#include <memory>

#include "queuelistmodel.hh" // QueueListModel + song/album
#include "queuelistitem.hh"  // QueueListItem::QueueDisplayMode

class QWidget;
class QLabel;
class QSplitter;
class FWMark;
class QueueListView;
class BackendController;
class AlbumArtManager;
class LibraryManager;
class FavoritesManager;

// Owns the resizable queue panel slice extracted from MainWindow: the panel +
// splitter geometry, the show/hide slide animation, the queue model and its
// backend wiring, the header count label, and the display/style/color-band
// settings. MainWindow builds the bar widgets, constructs this with the .ui-owned
// QueueListView + the splitter's two host widgets, then forwards the few
// cross-cutting calls (current-index from playback state, the resize/close geometry
// hooks, settings changes) and connects visibility_changed() to the playback bar.
class QueuePanelController : public QObject {
  Q_OBJECT

public:
  QueuePanelController(QueueListView *queuelist, // .ui child, reparented into the panel
      QWidget *main_area,                        // mainarea_frame — the other splitter child
      QWidget *splitter_parent,                  // central_frame — parent of the splitter
      BackendController *backend,
      std::shared_ptr<AlbumArtManager> artman,
      std::shared_ptr<LibraryManager> libman,
      std::shared_ptr<FavoritesManager> favman,
      QObject *parent = nullptr);

  // Startup: show the panel and arm the deferred width restore (the splitter has
  // no valid geometry until the first resize).
  void set_initial_visible(bool visible);

  // Backend/playback-driven (forwarded by MainWindow).
  void set_queue(const QList<song> &songs); // model data + header count
  void set_current_index(int pos);          // model current index + scroll-on-change
  void repaint_list();                      // repaint after a library scan resolves names

  // Persist the current width (called from MainWindow's close override).
  void save_width();

  // User action (queue button / playback-bar toggle).
  void toggle_visible();

  // Settings changes forwarded from SettingsView.
  void set_display_mode(const QString &mode);
  void set_color_band(bool enabled);
  void set_style(const QString &style); // "dark" -> dark queue palette

  QWidget *panel() const { return queue_panel; }

signals:
  void visibility_changed(bool visible); // -> PlaybackView::set_queue_active

protected:
  // Restores the saved width on the splitter's first laid-out resize.
  bool eventFilter(QObject *obj, QEvent *event) override;

private:
  void build_panel(QWidget *main_area, QWidget *splitter_parent);
  void apply_saved_settings();
  void apply_style(bool dark);
  void apply_display_mode();

  QueueListView *queuelist;
  BackendController *backend;
  std::shared_ptr<AlbumArtManager> artman;
  std::shared_ptr<LibraryManager> libman;
  std::shared_ptr<FavoritesManager> favman;

  std::unique_ptr<QueueListModel> queue_model;
  QWidget *queue_panel = nullptr;
  QSplitter *content_splitter = nullptr;
  QLabel *queue_count_label = nullptr;
  FWMark *queue_mark = nullptr;

  // Width the panel opens to; tracks the user's last dragged size so the show
  // animation slides back to it. Persisted via Settings::queue_width.
  int queue_open_width = 260;
  // Set when the queue starts visible: the splitter eventFilter restores the
  // saved width on its first laid-out resize, then clears this.
  bool queue_width_restore_pending = false;
  // Last position the list was auto-scrolled to; gates scroll-on-track-change.
  int last_scrolled_pos = -1;

  // Saved queue-display preference string, may be "auto".
  QString display_pref;
  // Cached from PlaybackState; drives auto_display_mode when display_pref == "auto".
  bool shuffle = false;
  // Mirrors what set_display_mode() was last called with, so apply_display_mode()
  // can skip a no-op repaint. Matches the delegate's own default.
  QueueListItem::QueueDisplayMode applied_mode = QueueListItem::QueueDisplayMode::Grouped;
};

// Map a saved queue-display preference string to the display enum (unknown -> the
// Grouped default). Factored out so the mapping is unit-testable without a panel.
namespace queuepanel {
auto display_mode_from_string(const QString &s) -> QueueListItem::QueueDisplayMode;
// Resolve the "auto" preference: Grouped iff the queue reads as album
// listening. Shuffle forces Full.
auto auto_display_mode(const QList<song> &songs, bool shuffle) -> QueueListItem::QueueDisplayMode;
}

#endif // QUEUEPANELCONTROLLER_HH
