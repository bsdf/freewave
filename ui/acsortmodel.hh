#ifndef ACSORTMODEL_HH
#define ACSORTMODEL_HH

#include <QDateTime>
#include <QList>
#include <QModelIndex>
#include <QRegularExpression>
#include <QSet>
#include <QSortFilterProxyModel>

class AlbumCoverSortModel : public QSortFilterProxyModel {
  Q_OBJECT

public:
  explicit AlbumCoverSortModel(QObject *parent = nullptr);
  void setSourceModel(QAbstractItemModel *model) override;

protected:
  bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

public slots:
  void set_search_filter(const QString &txt);
  void set_date_filter(const QDateTime &datetime);
  void clear_date_filter();
  void set_most_recent(uint32_t count);
  void clear_most_recent();

private:
  // Qt 6.9 added begin/endFilterChange and deprecated invalidateFilter(); Qt 6.8
  // (Debian stable, our floor) has only the latter. Bracket every filter-state
  // mutation with these two so both versions build warning-free.
  void begin_filter_change()
  {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    beginFilterChange();
#endif
  }
  void end_filter_change()
  {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    endFilterChange();
#else
    invalidateFilter();
#endif
  }

  void sort_default();
  void recompute_recent_rows();

  QDateTime filter_date;
  bool filter_by_date = false;

  QList<QRegularExpression> filter_res;
  bool filter_by_txt = false;

  QSet<int> recent_rows;
  uint32_t most_recent_count = 50;
  bool filter_most_recent = false;
};

#endif /* ACSORTMODEL_HH */
