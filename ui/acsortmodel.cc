#include "acsortmodel.hh"

#include "aclistmodel.hh"

#include <algorithm>

AlbumCoverSortModel::AlbumCoverSortModel(QObject *parent)
  : QSortFilterProxyModel{parent}
{
  setFilterCaseSensitivity(Qt::CaseInsensitive);
  setFilterRole(AlbumCoverListModel::UserRoleSearchKey);

  setSortCaseSensitivity(Qt::CaseInsensitive);
  sort_default();
}

auto
AlbumCoverSortModel::setSourceModel(QAbstractItemModel *model) -> void
{
  QSortFilterProxyModel::setSourceModel(model);
  if (model)
    connect(model, &QAbstractItemModel::modelReset,
        this, &AlbumCoverSortModel::recompute_recent_rows);
}

auto
AlbumCoverSortModel::sort_default() -> void
{
  setSortRole(AlbumCoverListModel::UserRoleSortKey);
  sort(0, Qt::AscendingOrder);
}

auto
AlbumCoverSortModel::recompute_recent_rows() -> void
{
  begin_filter_change();
  recent_rows.clear();

  if (filter_most_recent)
    {
      auto *src = sourceModel();
      if (src)
        {
          int n = src->rowCount();
          QList<QPair<QDateTime, int>> dated;
          dated.reserve(n);
          for (int i = 0; i < n; i++)
            {
              auto idx = src->index(i, 0);
              auto date = idx.data(AlbumCoverListModel::UserRoleLastModified).value<QDateTime>();
              dated.append({date, i});
            }

          std::sort(dated.begin(), dated.end(),
              [](const auto &a, const auto &b) { return a.first > b.first; });

          auto limit = std::min((int)most_recent_count, (int)dated.size());
          for (int i = 0; i < limit; i++)
            recent_rows.insert(dated[i].second);
        }
    }

  end_filter_change();
}

auto
AlbumCoverSortModel::set_search_filter(const QString &txt) -> void
{
  begin_filter_change();
  filter_res.clear();
  filter_by_txt = !txt.isEmpty();
  // Escape each token so the search box does literal substring matching —
  // raw regex metacharacters (e.g. "(", "[", "*") would otherwise compile to an
  // invalid pattern and blank the whole grid with no feedback.
  for (const auto &part : txt.split(" ", Qt::SkipEmptyParts))
    filter_res.append(QRegularExpression(QRegularExpression::escape(part),
        QRegularExpression::CaseInsensitiveOption));
  end_filter_change();
}

auto
AlbumCoverSortModel::set_date_filter(const QDateTime &datetime) -> void
{
  begin_filter_change();
  filter_date = datetime;
  filter_by_date = true;
  end_filter_change();

  setSortRole(AlbumCoverListModel::UserRoleLastModified);
  sort(0, Qt::DescendingOrder);
}

auto
AlbumCoverSortModel::clear_date_filter() -> void
{
  begin_filter_change();
  filter_by_date = false;
  end_filter_change();
  sort_default();
}

auto
AlbumCoverSortModel::set_most_recent(uint32_t count) -> void
{
  filter_most_recent = true;
  most_recent_count = count;
  recompute_recent_rows();

  setSortRole(AlbumCoverListModel::UserRoleLastModified);
  sort(0, Qt::DescendingOrder);
}

auto
AlbumCoverSortModel::clear_most_recent() -> void
{
  begin_filter_change();
  filter_most_recent = false;
  recent_rows.clear();
  end_filter_change();
  sort_default();
}

auto
AlbumCoverSortModel::filterAcceptsRow(
    int sourceRow, const QModelIndex &sourceParent) const -> bool
{
  auto idx = sourceModel()->index(sourceRow, 0, sourceParent);

  if (filter_most_recent && !recent_rows.contains(sourceRow))
    return false;

  if (filter_by_date)
    {
      auto date = idx.data(AlbumCoverListModel::UserRoleLastModified).value<QDateTime>();
      // An undated album yields an invalid QDateTime, which compares less-than
      // every valid date — keep it rather than silently hide it.
      if (date.isValid() && date < filter_date)
        return false;
    }

  if (filter_by_txt)
    {
      auto key = idx.data(AlbumCoverListModel::UserRoleSearchKey).value<QString>();
      for (const auto &re : filter_res)
        if (!re.match(key).hasMatch())
          return false;
    }

  return true;
}
