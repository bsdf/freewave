#ifndef QUEUELISTMODEL_HH
#define QUEUELISTMODEL_HH

#include <QAbstractListModel>
#include <QStringList>
#include <QMimeData>
#include <QPixmap>
#include <QList>

#include <memory>

#include "model/album.hh"
#include "model/song.hh"
#include "controller/albumartmanager.hh"
#include "controller/librarymanager.hh"

class FavoritesManager;

class QueueListModel : public QAbstractListModel {
  Q_OBJECT

public:
  explicit QueueListModel(std::shared_ptr<AlbumArtManager> artman = nullptr, std::shared_ptr<LibraryManager> libman = nullptr, QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

  Qt::DropActions supportedDropActions() const override;
  Qt::ItemFlags flags(const QModelIndex &index) const override;

  QMimeData *mimeData(const QModelIndexList &indexes) const override;
  bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) const override;
  bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) override;
  QStringList mimeTypes() const override;

  void set_data(const QList<song> &songs);
  QList<song> get_data() const { return songs; };

  void set_current_index(uint32_t current);
  uint32_t current_index() const;

  // The favorites cache backing UserRoleFavorited. Connects favorite_changed /
  // favorites_reset to targeted dataChanged emissions so hearts repaint.
  void set_favorites(const FavoritesManager *favs);

  enum UserRoles {
    UserRoleSong = Qt::UserRole,
    UserRoleCurrentlyPlaying,
    UserRoleAlbumHash,
    UserRoleAlbumName,
    UserRoleAlbumArtist,
    UserRoleAccent,
    UserRoleGroupStart,
    UserRoleLocalNumber,
    UserRolePlayingSameAlbum,
    UserRoleFavorited,
    UserRoleFavoritesAvailable,
    UserRoleShowArtist,
  };

signals:
  void rearrange_queue(uint32_t target_idx, std::vector<uint32_t> indexes);
  void insert_queue_at(uint32_t target_idx, const QList<song> &songs);
  void insert_albums_at(uint32_t target_idx, const QList<album> &albums);

private:
  QList<song> songs;
  QPixmap default_cover;
  uint32_t idx = 0;

  std::shared_ptr<AlbumArtManager> artman;
  std::shared_ptr<LibraryManager> libman;
  const FavoritesManager *favs = nullptr;
};

#endif /* QUEUELISTMODEL_HH */
