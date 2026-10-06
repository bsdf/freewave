#include <gtest/gtest.h>

#include "controller/albumcolor.hh"

#include <QImage>
#include <QPixmap>

namespace {

auto
solid_pixmap(QColor c, int size = 128) -> QPixmap
{
  QImage img(size, size, QImage::Format_RGB32);
  img.fill(c);
  return QPixmap::fromImage(img);
}

// Fills each quadrant of a size×size image with a distinct color.
auto
quadrant_pixmap(QColor tl, QColor tr, QColor bl, QColor br, int size = 128) -> QPixmap
{
  QImage img(size, size, QImage::Format_RGB32);
  int half = size / 2;
  for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x)
      img.setPixelColor(x, y, (x < half) ? (y < half ? tl : bl) : (y < half ? tr : br));
  return QPixmap::fromImage(img);
}

auto
color_distance(QColor a, QColor b) -> double
{
  double dr = a.red() - b.red();
  double dg = a.green() - b.green();
  double db = a.blue() - b.blue();
  return std::sqrt(dr * dr + dg * dg + db * db);
}

} // namespace

TEST(AlbumColor, ExtractPaletteNullPixmapIsEmpty)
{
  EXPECT_TRUE(albumcolor::extract_palette(QPixmap{}).isEmpty());
}

TEST(AlbumColor, ExtractPaletteZeroCountIsEmpty)
{
  EXPECT_TRUE(albumcolor::extract_palette(solid_pixmap(Qt::red), 0).isEmpty());
}

TEST(AlbumColor, ExtractPaletteAchromaticCoverIsEmpty)
{
  // Pure white has no chromatic signal pixels under the same filter compute_accent uses.
  EXPECT_TRUE(albumcolor::extract_palette(solid_pixmap(Qt::white)).isEmpty());
}

TEST(AlbumColor, ExtractPaletteSolidCoverReturnsOneColor)
{
  auto palette = albumcolor::extract_palette(solid_pixmap(QColor(200, 40, 60)), 5);
  ASSERT_EQ(palette.size(), 1);
  EXPECT_LT(color_distance(palette[0], QColor(200, 40, 60)), 5.0);
}

TEST(AlbumColor, ExtractPaletteSeparatesFourDistinctQuadrants)
{
  QColor red(220, 30, 30);
  QColor green(30, 200, 30);
  QColor blue(30, 30, 220);
  QColor yellow(210, 200, 30);
  auto palette = albumcolor::extract_palette(
      quadrant_pixmap(red, green, blue, yellow), 4);

  ASSERT_EQ(palette.size(), 4);
  for (QColor expected : {red, green, blue, yellow})
    {
      bool found = std::any_of(palette.begin(), palette.end(), [&](QColor c) {
        return color_distance(c, expected) < 20.0;
      });
      EXPECT_TRUE(found) << "no palette color close to "
                         << expected.name().toStdString();
    }
}

TEST(AlbumColor, ExtractPaletteRespectsRequestedCountCeiling)
{
  auto palette = albumcolor::extract_palette(solid_pixmap(QColor(80, 180, 90)), 3);
  EXPECT_LE(palette.size(), 3);
}

TEST(AlbumColor, ExtractPaletteMostPopulatedFirst)
{
  // Three quadrants of blue, one of red — blue should dominate box population
  // and land first regardless of hue.
  QColor blue(40, 60, 220);
  QColor red(220, 40, 40);
  QImage img(128, 128, QImage::Format_RGB32);
  img.fill(blue);
  for (int y = 0; y < 32; ++y)
    for (int x = 0; x < 32; ++x)
      img.setPixelColor(x, y, red);
  auto palette = albumcolor::extract_palette(QPixmap::fromImage(img), 2);

  ASSERT_FALSE(palette.isEmpty());
  EXPECT_LT(color_distance(palette.first(), blue), 20.0);
}

TEST(AlbumColor, ExtractPaletteDropsNearDuplicateShades)
{
  // Four very close shades of the same blue — real-world median-cut on a
  // muted cover keeps splitting a single color family into slivers this
  // close together; the palette should collapse them rather than report
  // four "distinct" colors that all look the same.
  QColor shades[4] = {
      QColor(40, 60, 200), QColor(44, 64, 204),
      QColor(36, 56, 196), QColor(48, 68, 208)};
  QImage img(128, 128, QImage::Format_RGB32);
  for (int y = 0; y < 128; ++y)
    for (int x = 0; x < 128; ++x)
      img.setPixelColor(x, y, shades[(y / 32) % 4]);

  auto palette = albumcolor::extract_palette(QPixmap::fromImage(img), 4);
  ASSERT_FALSE(palette.isEmpty());
  for (qsizetype i = 0; i < palette.size(); ++i)
    for (qsizetype j = i + 1; j < palette.size(); ++j)
      EXPECT_GE(color_distance(palette[i], palette[j]), albumcolor::MIN_COLOR_SEPARATION);
}
