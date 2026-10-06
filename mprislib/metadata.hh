#ifndef MPRISLIB_METADATA_HH
#define MPRISLIB_METADATA_HH

#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <QDBusObjectPath>

namespace mpris {

// Typed builder for an MPRIS metadata map (a{sv}). Callers never hand-write the
// "mpris:"/"xesam:" key strings; chain the setters and call toMap().
//
// Only set values are emitted — an unset field is absent from the map (MPRIS
// treats a missing key as "unknown"). The one exception is mpris:trackid, which
// the spec marks REQUIRED; callers should always set it.
class Metadata {
public:
  auto trackId(const QDBusObjectPath &id) -> Metadata &; // mpris:trackid (object path)
  auto length_us(qlonglong us) -> Metadata &;            // mpris:length (microseconds)
  auto artUrl(const QUrl &url) -> Metadata &;            // mpris:artUrl
  auto title(const QString &t) -> Metadata &;            // xesam:title
  auto artist(const QStringList &a) -> Metadata &;       // xesam:artist (LIST)
  auto album(const QString &a) -> Metadata &;            // xesam:album
  auto albumArtist(const QStringList &a) -> Metadata &;  // xesam:albumArtist (LIST)
  auto trackNumber(int n) -> Metadata &;                 // xesam:trackNumber
  auto discNumber(int n) -> Metadata &;                  // xesam:discNumber

  auto toMap() const -> QVariantMap { return map; }

private:
  QVariantMap map;
};

} // namespace mpris

#endif // MPRISLIB_METADATA_HH
