#include "mprislib/metadata.hh"

namespace mpris {

auto
Metadata::trackId(const QDBusObjectPath &id) -> Metadata &
{
  map[QStringLiteral("mpris:trackid")] = QVariant::fromValue(id);
  return *this;
}

auto
Metadata::length_us(qlonglong us) -> Metadata &
{
  map[QStringLiteral("mpris:length")] = us;
  return *this;
}

auto
Metadata::artUrl(const QUrl &url) -> Metadata &
{
  map[QStringLiteral("mpris:artUrl")] = url.toString();
  return *this;
}

auto
Metadata::title(const QString &t) -> Metadata &
{
  map[QStringLiteral("xesam:title")] = t;
  return *this;
}

auto
Metadata::artist(const QStringList &a) -> Metadata &
{
  map[QStringLiteral("xesam:artist")] = a;
  return *this;
}

auto
Metadata::album(const QString &a) -> Metadata &
{
  map[QStringLiteral("xesam:album")] = a;
  return *this;
}

auto
Metadata::albumArtist(const QStringList &a) -> Metadata &
{
  map[QStringLiteral("xesam:albumArtist")] = a;
  return *this;
}

auto
Metadata::trackNumber(int n) -> Metadata &
{
  map[QStringLiteral("xesam:trackNumber")] = n;
  return *this;
}

auto
Metadata::discNumber(int n) -> Metadata &
{
  map[QStringLiteral("xesam:discNumber")] = n;
  return *this;
}

} // namespace mpris
