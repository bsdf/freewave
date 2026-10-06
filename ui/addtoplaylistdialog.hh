#ifndef ADDTOPLAYLISTDIALOG_HH
#define ADDTOPLAYLISTDIALOG_HH

#include <memory>

#include <QDialog>
#include <QList>
#include <QMap>
#include <QString>

#include "model/album.hh"
#include "model/song.hh"

class QLabel;
class QLineEdit;
class QVBoxLayout;
class Backend;
class PlaylistsManager;
class AlbumArtManager;

// What is being added to a playlist. An album subject needs its full tracklist
// resolved (async on Subsonic) before membership/add can be computed; a track
// subject is self-contained.
struct playlist_subject {
  enum class Kind { Album,
    Track };
  Kind kind = Kind::Track;
  album alb; // always set (cover, artist, title)
  song trk;  // set when kind == Track
};

// Modal "Add to playlist…" picker: pick an existing playlist to toggle the
// subject in/out of, or type a name to create a new one seeded with the subject.
// Toggles apply live; Cancel/Done both just close. Deletes itself on close.
class AddToPlaylistDialog : public QDialog {
  Q_OBJECT

public:
  AddToPlaylistDialog(std::shared_ptr<PlaylistsManager> plman,
      std::shared_ptr<AlbumArtManager> artman, Backend *backend,
      playlist_subject subject, QWidget *parent = nullptr);
  ~AddToPlaylistDialog();

protected:
  bool eventFilter(QObject *obj, QEvent *event) override;

private:
  class Row; // one playlist entry in the list

  void rebuild_rows();
  void reload_contents(); // ensure_songs for every playlist
  void refresh_row(const QString &id);
  void refresh_mosaic(const QString &id); // rebuild one row's tile
  void refresh_mosaics();                 // rebuild every row's tile
  void apply_filter();
  void update_footer();
  void toggle(const QString &id);
  void create_from_term();

  // subject_songs resolved? (album subjects fetch asynchronously). Membership
  // and toggles are disabled until this is true.
  auto subject_ready() const -> bool { return resolved; }
  // uris of the subject's songs present in `playlist` (any order).
  auto membership_positions(const QList<song> &playlist) const -> QList<int>;
  auto is_member(const QList<song> &playlist) const -> bool;

  std::shared_ptr<PlaylistsManager> plman;
  std::shared_ptr<AlbumArtManager> artman;
  Backend *backend;
  playlist_subject subject;

  QList<song> subject_songs;
  bool resolved = false;

  QLineEdit *search;
  QWidget *new_row;
  QLabel *new_label;
  QWidget *list_container;
  QVBoxLayout *list_layout;
  QLabel *empty_label;
  QLabel *footer_count;

  QMap<QString, Row *> rows; // by playlist id
  // Last resolved membership per playlist id, preserved across the rebuilds
  // triggered by playlists_reset so a mid-refetch (contents momentarily
  // dropped) keeps showing the known state instead of flickering to loading.
  QMap<QString, bool> known_member;
};

#endif // ADDTOPLAYLISTDIALOG_HH
