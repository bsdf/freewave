#ifndef SONGLISTMODEL_HH
#define SONGLISTMODEL_HH

#include <QAbstractListModel>
#include <QStringList>
#include <QMimeData>
#include <QPixmap>
#include <QList>

#include <map>
#include <memory>

#include "model/song.hh"
#include "controller/albumartmanager.hh"

class FavoritesManager;

class SongListModel : public QAbstractListModel {
  Q_OBJECT

public:
  explicit SongListModel(QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

  Qt::ItemFlags flags(const QModelIndex &index) const override;
  QMimeData *mimeData(const QModelIndexList &indexes) const override;
  QStringList mimeTypes() const override;

  void set_data(const QList<song> &songs);
  QList<song> get_data() const { return songs; };

  // Show `count` placeholder rows while the real tracklist loads.
  void set_skeleton(int count);

  void set_current_uri(const QString &current);
  QString current_uri() const { return uri; };

  void set_show_artist(bool show);

  // The favorites cache backing UserRoleFavorited. Connects favorite_changed /
  // favorites_reset to targeted dataChanged emissions so hearts repaint.
  void set_favorites(const FavoritesManager *favs);

  enum UserRoles {
    UserRoleSong = Qt::UserRole,
    UserRoleCurrentlyPlaying,
    UserRoleAlbumHash,
    UserRoleMultiDisc,
    UserRoleDiscOpener,
    UserRoleShowArtist,
    UserRoleSkeleton,
    UserRoleFavorited,
    UserRoleFavoritesAvailable,
  };

private:
  QList<song> songs;
  QString uri;
  std::map<uint32_t, uint32_t> disc_openers;
  bool multi_disc = false;
  bool show_artist = false;
  int skeleton_count = 0; // placeholder rows shown when songs is empty
  const FavoritesManager *favs = nullptr;
};

#endif /* SONGLISTMODEL_HH */
