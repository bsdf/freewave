#include <gtest/gtest.h>

#include "mprislib/metadata.hh"

#include <QVariantMap>
#include <QStringList>
#include <QDBusObjectPath>

using mpris::Metadata;

// The spec key names are an external contract — controllers index on these exact
// strings, so pin every one.
TEST(MprisMetadata, KeyNames)
{
  Metadata m;
  m.trackId(QDBusObjectPath("/org/mpris/freewave/track/1"))
      .length_us(1000)
      .artUrl(QUrl::fromLocalFile("/tmp/cover.jpg"))
      .title("Song")
      .artist({"Artist"})
      .album("Album")
      .albumArtist({"AlbumArtist"})
      .trackNumber(3)
      .discNumber(2);
  const QVariantMap map = m.toMap();

  EXPECT_TRUE(map.contains("mpris:trackid"));
  EXPECT_TRUE(map.contains("mpris:length"));
  EXPECT_TRUE(map.contains("mpris:artUrl"));
  EXPECT_TRUE(map.contains("xesam:title"));
  EXPECT_TRUE(map.contains("xesam:artist"));
  EXPECT_TRUE(map.contains("xesam:album"));
  EXPECT_TRUE(map.contains("xesam:albumArtist"));
  EXPECT_TRUE(map.contains("xesam:trackNumber"));
  EXPECT_TRUE(map.contains("xesam:discNumber"));
}

// Unset fields must be absent (MPRIS reads a missing key as "unknown") — an
// empty entry is not the same as no entry.
TEST(MprisMetadata, UnsetFieldsAbsent)
{
  Metadata m;
  m.title("Only Title");
  const QVariantMap map = m.toMap();

  EXPECT_EQ(map.size(), 1);
  EXPECT_TRUE(map.contains("xesam:title"));
  EXPECT_FALSE(map.contains("xesam:artist"));
  EXPECT_FALSE(map.contains("mpris:trackid"));
}

// trackid is an object path (D-Bus type 'o'), not a string.
TEST(MprisMetadata, TrackIdIsObjectPath)
{
  Metadata m;
  m.trackId(QDBusObjectPath("/org/mpris/freewave/track/7"));
  const QVariant v = m.toMap().value("mpris:trackid");

  ASSERT_EQ(v.metaType().id(), qMetaTypeId<QDBusObjectPath>());
  EXPECT_EQ(v.value<QDBusObjectPath>().path(), "/org/mpris/freewave/track/7");
}

// artist / albumArtist are lists ('as'), never bare strings.
TEST(MprisMetadata, ArtistFieldsAreLists)
{
  Metadata m;
  m.artist({"A", "B"}).albumArtist({"C"});
  const QVariantMap map = m.toMap();

  ASSERT_EQ(map.value("xesam:artist").metaType().id(), qMetaTypeId<QStringList>());
  EXPECT_EQ(map.value("xesam:artist").toStringList(), (QStringList{"A", "B"}));
  EXPECT_EQ(map.value("xesam:albumArtist").metaType().id(), qMetaTypeId<QStringList>());
}

// length is stored verbatim in microseconds; the ms→µs conversion is the
// caller's job (verified in the bridge tests).
TEST(MprisMetadata, LengthIsMicroseconds)
{
  Metadata m;
  m.length_us(180'000'000); // 180 s
  EXPECT_EQ(m.toMap().value("mpris:length").toLongLong(), 180'000'000);
}

// artUrl is marshalled as a string ('s'), per the MPRIS metadata schema.
TEST(MprisMetadata, ArtUrlIsString)
{
  Metadata m;
  m.artUrl(QUrl::fromLocalFile("/cache/abc.jpg"));
  const QVariant v = m.toMap().value("mpris:artUrl");
  EXPECT_EQ(v.metaType().id(), qMetaTypeId<QString>());
  EXPECT_EQ(v.toString(), "file:///cache/abc.jpg");
}
