#ifndef ALBUMCOVERLISTMODEL_HH
#define ALBUMCOVERLISTMODEL_HH

#include <QList>
#include <QMimeData>
#include <QStringList>
#include <QAbstractListModel>

#include <memory>

#include "controller/albumartmanager.hh"
#include "model/album.hh"

class AlbumCoverListModel : public QAbstractListModel {
  Q_OBJECT

public:
  explicit AlbumCoverListModel(
      std::shared_ptr<AlbumArtManager> artman, QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
  Qt::ItemFlags flags(const QModelIndex &index) const override;

  QStringList mimeTypes() const override;
  QMimeData *mimeData(const QModelIndexList &indexes) const override;

  void set_data(const QList<album> &albums);
  QList<album> get_data() const { return albums; };

  enum UserRoles {
    UserRoleAlbum = Qt::UserRole,
    UserRoleLastModified,
    UserRoleSortKey,
    UserRoleSearchKey,
  };

public slots:
  void set_cover_size(QSize size);

private:
  QList<album> albums;
  QStringList covers;
  QPixmap default_cover;
  QSize cover_size{200, 200};

  std::shared_ptr<AlbumArtManager> artman;
};

#endif // ALBUMCOVERLISTMODEL_HH
