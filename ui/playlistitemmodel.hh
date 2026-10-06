#ifndef PLAYLISTITEMMODEL_HH
#define PLAYLISTITEMMODEL_HH

#include <QAbstractListModel>
#include <QList>
#include <QString>

#include "controller/librarymanager.hh"
#include "controller/playlistrows.hh"
#include "model/song.hh"

class FavoritesManager;

// The detail-pane model for one playlist's contents. Holds the flat song list
// plus the display-time coalescing (playlistrows) that folds a full-album run
// into a single Album row — one model row per playlist_row. Playback, removal
// and reorder always operate on flat song positions; the row->span mapping
// lives here only (coalescing is display-only).
class PlaylistItemModel : public QAbstractListModel {
  Q_OBJECT
public:
  explicit PlaylistItemModel(const LibraryManager *lib, QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

  // Drag-reorder. Rows drag as whole display rows (an Album row carries its full
  // coalesced span); on drop, the moved rows are expanded to their flat song
  // indexes and emitted as rearrange_rows for the view to forward to the manager.
  // dropMimeData returns false (no default row surgery) — the authoritative
  // refetch / optimistic cache reorder rebuilds the model, mirroring the queue.
  Qt::ItemFlags flags(const QModelIndex &index) const override;
  Qt::DropActions supportedDropActions() const override;
  QStringList mimeTypes() const override;
  QMimeData *mimeData(const QModelIndexList &indexes) const override;
  bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row,
      int column, const QModelIndex &parent) override;

  void set_data(const QList<song> &songs);
  auto songs() const -> QList<song> { return flat; }

  // Flat song index where display row `row` begins (-1 if out of range).
  auto flat_index(int row) const -> int;

  void set_current_uri(const QString &uri);
  // Backing store for UserRoleFavorited; wires favorite_changed / favorites_reset
  // to targeted dataChanged so Track-row hearts repaint.
  void set_favorites(const FavoritesManager *favs);

  enum UserRoles {
    UserRoleKind = Qt::UserRole, // 0 = Track, 1 = Album
    UserRoleSong,                // the representative song (Track rows)
    UserRoleAlbumHash,           // art key (Track: song hash; Album: album hash)
    UserRolePrimary,             // Track: title; Album: album name
    UserRoleSecondary,           // Track: artist～album; Album: artist～N tracks
    UserRoleFirstIndex,          // flat index of the row's first track
    UserRoleSpan,                // 1 for Track; span length for Album
    UserRoleDurationMs,          // Track: song ms; Album: summed span ms
    UserRoleCurrentlyPlaying,    // current uri falls within this row
    UserRoleFavorited,           // Track rows only
    UserRoleFavoritesAvailable,
  };

signals:
  // A drag-reorder landed: move the songs at flat indexes `moved` to flat
  // position `target` (insertion point in pre-move index space). The view
  // forwards this to PlaylistsManager::rearrange for the selected playlist.
  void rearrange_rows(int target, const QList<int> &moved);

private:
  enum Kind { Track = 0,
    Album = 1 };
  struct DisplayRow {
    Kind kind;
    int first;
    int count;
    QString album_hash;
    QString primary;
    QString secondary;
    song track; // valid for Track rows
    qint64 duration_ms;
    bool playing;
  };

  void rebuild();
  void refresh_favorites();

  QList<song> flat;
  QList<DisplayRow> rows;
  const LibraryManager *lib;
  const FavoritesManager *favs = nullptr;
  QString current_uri;
};

#endif // PLAYLISTITEMMODEL_HH
