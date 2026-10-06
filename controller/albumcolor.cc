#include "controller/albumcolor.hh"

#include <QImage>
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

namespace albumcolor {

namespace {
constexpr int BUCKETS = 24; // hue buckets (15° each)
constexpr int SAMPLE_SIZE = 64;
constexpr int MIN_SCORE = 1;

// Same "is this pixel worth keeping" test compute_accent applies before
// bucketing: drop achromatic, too-dark, too-bright, and too-grey pixels.
auto
is_signal_pixel(QColor c) -> bool
{
  int h, s, v;
  c.getHsv(&h, &s, &v);
  return h >= 0 && v >= 40 && v <= 240 && s >= 50;
}

struct rgb_pixel {
  int r, g, b;
};

using color_box = std::vector<rgb_pixel>;

auto
channel_range(const color_box &box) -> int
{
  auto [r_lo, r_hi] = std::ranges::minmax(box, {}, &rgb_pixel::r);
  auto [g_lo, g_hi] = std::ranges::minmax(box, {}, &rgb_pixel::g);
  auto [b_lo, b_hi] = std::ranges::minmax(box, {}, &rgb_pixel::b);
  return std::max({r_hi.r - r_lo.r, g_hi.g - g_lo.g, b_hi.b - b_lo.b});
}

// A box with fewer than 2 pixels, or whose pixels are all the same color,
// has nothing left to split along.
auto
is_splittable(const color_box &box) -> bool
{
  return box.size() >= 2 && channel_range(box) > 0;
}

// Split `box` at the median along whichever channel has the widest range —
// the classic median-cut heuristic. Caller guarantees is_splittable(box).
auto
split_box(color_box box) -> std::pair<color_box, color_box>
{
  auto [r_lo, r_hi] = std::ranges::minmax(box, {}, &rgb_pixel::r);
  auto [g_lo, g_hi] = std::ranges::minmax(box, {}, &rgb_pixel::g);
  auto [b_lo, b_hi] = std::ranges::minmax(box, {}, &rgb_pixel::b);
  int r_range = r_hi.r - r_lo.r;
  int g_range = g_hi.g - g_lo.g;
  int b_range = b_hi.b - b_lo.b;

  if (r_range >= g_range && r_range >= b_range)
    std::ranges::sort(box, {}, &rgb_pixel::r);
  else if (g_range >= b_range)
    std::ranges::sort(box, {}, &rgb_pixel::g);
  else
    std::ranges::sort(box, {}, &rgb_pixel::b);

  auto mid = box.begin() + static_cast<std::ptrdiff_t>(box.size() / 2);
  return {color_box(box.begin(), mid), color_box(mid, box.end())};
}

auto
mean_color(const color_box &box) -> QColor
{
  double r = 0, g = 0, b = 0;
  for (const auto &p : box)
    {
      r += p.r;
      g += p.g;
      b += p.b;
    }
  double n = static_cast<double>(box.size());
  return QColor(static_cast<int>(r / n), static_cast<int>(g / n), static_cast<int>(b / n));
}
}

auto
compute_accent(const QPixmap &cover) -> QColor
{
  if (cover.isNull() || cover.width() == 0 || cover.height() == 0)
    return QColor{};

  QImage img = cover.toImage()
                   .scaled(SAMPLE_SIZE, SAMPLE_SIZE,
                       Qt::IgnoreAspectRatio,
                       Qt::SmoothTransformation)
                   .convertToFormat(QImage::Format_RGB32);

  std::array<double, BUCKETS> weight{};
  std::array<double, BUCKETS> sum_r{};
  std::array<double, BUCKETS> sum_g{};
  std::array<double, BUCKETS> sum_b{};

  for (int y = 0; y < img.height(); ++y)
    {
      const auto *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
      for (int x = 0; x < img.width(); ++x)
        {
          QColor c = QColor::fromRgb(line[x]);
          int h, s, v;
          c.getHsv(&h, &s, &v);
          if (h < 0) continue;             // achromatic
          if (v < 40 || v > 240) continue; // too dark/bright
          if (s < 50) continue;            // too grey

          double score = (s / 255.0) * (v / 255.0);
          int bucket = (h * BUCKETS / 360) % BUCKETS;
          weight[bucket] += score;
          sum_r[bucket] += c.redF() * score;
          sum_g[bucket] += c.greenF() * score;
          sum_b[bucket] += c.blueF() * score;
        }
    }

  int best = -1;
  double best_w = 0.0;
  for (int i = 0; i < BUCKETS; ++i)
    {
      if (weight[i] > best_w)
        {
          best_w = weight[i];
          best = i;
        }
    }

  if (best < 0 || best_w < MIN_SCORE)
    return QColor{}; // no dominant chromatic color found

  QColor accent = QColor::fromRgbF(
      sum_r[best] / best_w,
      sum_g[best] / best_w,
      sum_b[best] / best_w);
  return accent;
}

auto
extract_palette(const QPixmap &cover, int count) -> QList<QColor>
{
  if (cover.isNull() || cover.width() == 0 || cover.height() == 0 || count <= 0)
    return {};

  QImage img = cover.toImage()
                   .scaled(SAMPLE_SIZE, SAMPLE_SIZE,
                       Qt::IgnoreAspectRatio,
                       Qt::SmoothTransformation)
                   .convertToFormat(QImage::Format_RGB32);

  color_box pixels;
  pixels.reserve(static_cast<size_t>(img.width() * img.height()));
  for (int y = 0; y < img.height(); ++y)
    {
      const auto *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
      for (int x = 0; x < img.width(); ++x)
        {
          QColor c = QColor::fromRgb(line[x]);
          if (is_signal_pixel(c))
            pixels.push_back({c.red(), c.green(), c.blue()});
        }
    }
  if (pixels.empty())
    return {};

  std::vector<color_box> boxes;
  boxes.push_back(std::move(pixels));
  // Oversplit past `count` so the distinct-color pass below has enough
  // candidate boxes to skip near-duplicates and still fill `count` slots.
  int split_target = std::max(count * 4, 16);
  while (static_cast<int>(boxes.size()) < split_target)
    {
      // The most populous splittable box, not just the most populous box —
      // a large box of uniform color has nothing left to split, but a
      // smaller box with real variation still does.
      auto biggest = boxes.end();
      for (auto it = boxes.begin(); it != boxes.end(); ++it)
        if (is_splittable(*it) && (biggest == boxes.end() || it->size() > biggest->size()))
          biggest = it;
      if (biggest == boxes.end())
        break;

      auto [a, b] = split_box(std::move(*biggest));
      *biggest = std::move(a);
      boxes.push_back(std::move(b));
    }

  std::ranges::sort(boxes, std::ranges::greater{}, &color_box::size);

  QList<QColor> palette;
  for (const auto &box : boxes)
    {
      if (palette.size() >= count)
        break;
      QColor c = mean_color(box);
      bool too_close = std::ranges::any_of(palette, [&](QColor kept) {
        return color_distance(kept, c) < MIN_COLOR_SEPARATION;
      });
      if (!too_close)
        palette.push_back(c);
    }
  return palette;
}

auto
color_distance(QColor a, QColor b) -> double
{
  double dr = a.red() - b.red();
  double dg = a.green() - b.green();
  double db = a.blue() - b.blue();
  return std::sqrt(dr * dr + dg * dg + db * db);
}

}
