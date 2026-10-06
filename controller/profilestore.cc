#include "controller/profilestore.hh"
#include <QSettings>
#include <algorithm>

auto
ProfileStore::load() const -> QList<BackendProfile>
{
  QList<BackendProfile> out;
  QSettings s;
  int n = s.beginReadArray("profiles");
  for (int i = 0; i < n; ++i)
    {
      s.setArrayIndex(i);
      BackendProfile p{.id = s.value("id").toString(),
          .type = s.value("type").toString(),
          .name = s.value("name").toString()};
      for (const QString &k : s.childKeys())
        {
          if (k == "id" || k == "type" || k == "name")
            continue;
          p.params.insert(k, s.value(k));
        }
      out.append(p);
    }
  s.endArray();
  return out;
}

void
ProfileStore::save(const QList<BackendProfile> &profiles)
{
  QSettings s;
  s.remove("profiles");
  s.beginWriteArray("profiles");
  for (int i = 0; i < profiles.size(); ++i)
    {
      s.setArrayIndex(i);
      const BackendProfile &p = profiles.at(i);
      s.setValue("id", p.id);
      s.setValue("type", p.type);
      s.setValue("name", p.name);
      for (auto it = p.params.constBegin(); it != p.params.constEnd(); ++it)
        {
          s.setValue(it.key(), it.value());
        }
    }
  s.endArray();
}

auto
ProfileStore::active_id() const -> QString
{
  QSettings s;
  return s.value("active_profile").toString();
}

void
ProfileStore::set_active_id(const QString &id)
{
  QSettings s;
  s.setValue("active_profile", id);
}

auto
ProfileStore::profile_by_id(const QString &id) const -> std::optional<BackendProfile>
{
  for (const BackendProfile &p : load())
    if (p.id == id)
      return p;
  return std::nullopt;
}

auto
ProfileStore::active_profile() const -> std::optional<BackendProfile>
{
  QString aid = active_id();
  auto profiles = load();
  for (const BackendProfile &p : profiles)
    {
      if (p.id == aid)
        return p;
    }
  if (profiles.size() == 1)
    return profiles.at(0);
  return std::nullopt;
}

void
ProfileStore::add(const BackendProfile &p)
{
  auto ps = load();
  ps.append(p);
  save(ps);
}

void
ProfileStore::update(const BackendProfile &p)
{
  auto ps = load();
  if (auto it = std::ranges::find(ps, p.id, &BackendProfile::id); it != ps.end())
    *it = p;
  save(ps);
}

void
ProfileStore::remove(const QString &id)
{
  auto ps = load();
  if (auto it = std::ranges::find(ps, id, &BackendProfile::id); it != ps.end())
    ps.erase(it);
  save(ps);
  if (active_id() == id)
    {
      if (ps.size() > 0)
        set_active_id(ps.at(0).id);
      else
        set_active_id("");
    }
}
