#include "ui/textcover.hh"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>
#include <QSizeF>
#include <QTextLayout>
#include <QTextOption>

#include "ui/theme.hh"

namespace textcover {

// FNV-1a 32-bit hash over QChar unicode values
static auto
fnv1a(const QString &s) -> uint32_t
{
  uint32_t h = 2166136261u;
  for (const QChar c : s)
    {
      h ^= static_cast<uint32_t>(c.unicode());
      h *= 16777619u;
    }
  return h;
}

auto
deterministic_accent(const QString &key) -> QColor
{
  uint32_t h = fnv1a(key);
  return QColor::fromHslF((h % 360) / 360.0, 0.45, 0.52);
}

auto
deterministic_palette(const QString &key, int count) -> QList<QColor>
{
  if (count <= 0)
    return {};

  uint32_t h = fnv1a(key);
  qreal base_hue = (h % 360) / 360.0;
  QList<QColor> palette;
  palette.reserve(count);
  for (int i = 0; i < count; ++i)
    {
      qreal hue = std::fmod(base_hue + (static_cast<qreal>(i) / count), 1.0);
      palette.push_back(QColor::fromHslF(hue, 0.45, 0.52));
    }
  return palette;
}

// Component-wise linear interpolation: returns a*t + b*(1-t)
static auto
mix(QColor a, QColor b, qreal t) -> QColor
{
  return QColor::fromRgbF(
      a.redF() * t + b.redF() * (1.0 - t),
      a.greenF() * t + b.greenF() * (1.0 - t),
      a.blueF() * t + b.blueF() * (1.0 - t));
}

// Metadata face — sourced from the app's configured mono (IBM Plex Mono); the
// sleeve keeps its own uppercasing/tracking treatment on top.
static auto
mono_font(int weight, qreal pixelSize) -> QFont
{
  QFont f = theme::mono(static_cast<int>(pixelSize));
  f.setWeight(static_cast<QFont::Weight>(weight));
  f.setCapitalization(QFont::AllUppercase);
  f.setLetterSpacing(QFont::AbsoluteSpacing, 0.1 * pixelSize);
  return f;
}

// Title face — sourced from the app's configured display family (IBM Plex
// Sans); italic demibold for the editorial display treatment.
static auto
serif_font(qreal pixelSize) -> QFont
{
  QFont f = theme::display_serif(static_cast<int>(pixelSize));
  f.setWeight(QFont::DemiBold);
  f.setItalic(true);
  return f;
}

// First letter of each of the first two words of the artist (falling back to
// the title), uppercased — e.g. "David Sylvian" → "DS", "Radiohead" → "R".
static auto
initials(const QString &artist, const QString &title) -> QString
{
  const QString &src = !artist.isEmpty() ? artist : title;
  QString out;
  for (const QString &word : src.split(' ', Qt::SkipEmptyParts))
    {
      for (const QChar c : word)
        if (c.isLetterOrNumber())
          {
            out.append(c.toUpper());
            break;
          }
      if (out.size() >= 2) break;
    }
  if (out.isEmpty() && !src.isEmpty())
    out.append(src.at(0).toUpper());
  return out;
}

static auto
artist_scale(int len) -> qreal
{
  if (len > 26) return 0.68;
  if (len > 18) return 0.78;
  if (len > 13) return 0.88;
  return 1.0;
}

// Largest integer pixel size in [minPx, maxPx] at which `text`, word-wrapped to
// `region.width()`, fits within `region.height()`. Returns minPx when even that
// overflows — the caller then truncates. Binary search; only runs on a cache miss.
static auto
fit_font_size(const QString &text, QFont proto, QSizeF region,
    qreal minPx, qreal maxPx, QPaintDevice *device) -> qreal
{
  int lo = std::max(1, int(std::floor(minPx)));
  int hi = std::max(lo, int(std::floor(maxPx)));
  int best = lo;
  while (lo <= hi)
    {
      const int mid = (lo + hi) / 2;
      proto.setPixelSize(mid);
      const qreal lineH = QFontMetricsF(proto, device).lineSpacing();

      QTextLayout layout(text, proto, device);
      QTextOption opt;
      opt.setWrapMode(QTextOption::WordWrap);
      layout.setTextOption(opt);
      int lines = 0;
      qreal maxLineW = 0.0;
      layout.beginLayout();
      for (;;)
        {
          QTextLine line = layout.createLine();
          if (!line.isValid()) break;
          line.setLineWidth(region.width());
          maxLineW = std::max(maxLineW, line.naturalTextWidth());
          ++lines;
        }
      layout.endLayout();

      // Must fit vertically AND horizontally — an unbreakable long word can
      // exceed the region width even though word-wrap left it on its own line.
      if (lines * lineH <= region.height() && maxLineW <= region.width())
        {
          best = mid;
          lo = mid + 1;
        }
      else
        hi = mid - 1;
    }
  return best;
}

// Word-wrap `text` in `rect` with `font`/`color`, clamped to `maxLines` (also
// bounded by rect height). Elides the last visible line with "…" on overflow.
// Returns the vertical height actually used, so callers can stack below it.
static auto
draw_clamped(QPainter &p, const QRectF &rect, const QFont &font,
    const QColor &color, const QString &text, int maxLines, int hAlign) -> qreal
{
  if (text.isEmpty()) return 0.0;
  p.setFont(font);
  p.setPen(color);
  QFontMetricsF fm(font);
  const qreal lineH = fm.lineSpacing();
  const int maxByHeight = std::max(1, int(std::floor(rect.height() / lineH)));
  const int limit = std::max(1, std::min(maxLines, maxByHeight));

  QTextLayout layout(text, font, p.device());
  QTextOption opt;
  opt.setWrapMode(QTextOption::WordWrap);
  layout.setTextOption(opt);

  struct LineInfo {
    int start;
    int length;
  };
  std::vector<LineInfo> infos;
  layout.beginLayout();
  for (;;)
    {
      QTextLine line = layout.createLine();
      if (!line.isValid()) break;
      line.setLineWidth(rect.width());
      infos.push_back({line.textStart(), line.textLength()});
    }
  layout.endLayout();

  qreal y = rect.top();
  int drawn = 0;
  for (int i = 0; i < int(infos.size()) && drawn < limit; ++i)
    {
      const bool lastAllowed = (drawn == limit - 1);
      const bool moreAfter = (i < int(infos.size()) - 1);
      QString lineText = text.mid(infos[i].start, infos[i].length).trimmed();
      if (lastAllowed && moreAfter)
        lineText = fm.elidedText(text.mid(infos[i].start).trimmed(),
            Qt::ElideRight, rect.width());
      p.drawText(QRectF(rect.left(), y, rect.width(), lineH),
          Qt::Alignment(hAlign) | Qt::AlignTop, lineText);
      y += lineH;
      ++drawn;
    }
  return y - rect.top();
}

static auto
paint_sleeve(QPainter &p, const QString &artist,
    const QString &title, const QString &year,
    const QColor &accent, QSizeF sz, bool stamp, int radius) -> void
{
  const qreal W = sz.width();

  // Named colors
  const QColor PAPER(0xf3, 0xef, 0xe7);
  const QColor INK(0x1a, 0x18, 0x15);
  const QColor frameBase(0xd8, 0xd2, 0xc7);
  const QColor softBase(0x6b, 0x63, 0x58);

  const QColor frameColor = mix(accent, frameBase, 0.32);
  const QColor markInk = mix(accent, INK, 0.62);
  const QColor softInk = mix(accent, softBase, 0.40);

  // Background rounded rect
  p.setPen(Qt::NoPen);
  p.setBrush(PAPER);
  QPainterPath bg;
  bg.addRoundedRect(QRectF(QPointF(0, 0), sz), radius, radius);
  p.drawPath(bg);

  // Frame: 1px border inset 8.5% from all sides
  const qreal frameInset = 0.085 * W;
  QRectF frameRect = QRectF(QPointF(0, 0), sz).adjusted(frameInset, frameInset, -frameInset, -frameInset);
  p.setPen(QPen(frameColor, 1.0));
  p.setBrush(Qt::NoBrush);
  p.drawRect(frameRect);

  // Content rect = frame rect inset by padding 7.5%
  const qreal pad = 0.075 * W;
  QRectF contentRect = frameRect.adjusted(pad, pad, -pad, -pad);

  // ── Footer band (bottom) ──────────────────────────────────────────────────
  const qreal footerMonoPs = 5.0 * W / 100.0;
  QFont fmf = mono_font(500, footerMonoPs);
  QFontMetricsF fmfm(fmf);
  const qreal footerLineH = fmfm.height();
  const qreal footerPadTop = 4.0 * W / 100.0;
  const qreal footerTotalH = footerPadTop + footerLineH + 2.0; // 2px buffer

  // With no year there's nothing to footer: drop the band entirely and let the
  // title reclaim the full content height (titleBottom below).
  const bool hasYear = !year.isEmpty();
  const qreal footerY
      = hasYear ? contentRect.bottom() - footerTotalH : contentRect.bottom();
  if (hasYear)
    {
      // top border across content width
      p.setPen(QPen(frameColor, 1.0));
      p.drawLine(QPointF(contentRect.left(), footerY),
          QPointF(contentRect.right(), footerY));

      // footer text
      p.setFont(fmf);
      p.setPen(softInk);
      QRectF footerTextRect(contentRect.left(), footerY + footerPadTop,
          contentRect.width(), footerLineH + 2.0);
      p.drawText(footerTextRect, Qt::AlignLeft | Qt::AlignTop, year);
    }

  // ── Artist band (top) ──────────────────────────────────────────────────────
  const qreal artistBase = 5.8 * W / 100.0;
  const qreal artistPs = artistBase * artist_scale(artist.length());
  QFont af = mono_font(500, artistPs);
  QFontMetricsF afm(af);
  const qreal artistLineH = afm.lineSpacing();

  // Artist rect: 2 lines tall, anchored at content top
  QRectF artistRect(contentRect.left(), contentRect.top(),
      contentRect.width(), artistLineH * 2.0);
  qreal artistUsedH = draw_clamped(p, artistRect, af, markInk, artist, 2, Qt::AlignLeft);

  // ── Title (fills from artist bottom down to footer rule) ──────────────────
  const qreal titleTop = contentRect.top() + artistUsedH + 0.03 * W;
  const qreal titleBottom = footerY - 0.02 * W;
  const qreal titleH = titleBottom - titleTop;

  if (titleH > 4.0)
    {
      QRectF titleRegion(contentRect.left(), titleTop,
          contentRect.width(), titleH);

      // Pick the largest size that fits the whole title in the region, down to a
      // readable floor; draw_clamped truncates if even the floor overflows.
      const qreal titleMaxPs = 18.0 * W / 100.0;
      const qreal titleMinPs = 6.0 * W / 100.0;
      const qreal titlePs = fit_font_size(title, serif_font(titleMaxPs),
          titleRegion.size(), titleMinPs, titleMaxPs, p.device());

      QFont tf = serif_font(titlePs);
      draw_clamped(p, titleRegion, tf, markInk, title, 99, Qt::AlignLeft);
    }

  // ── Stamp ────────────────────────────────────────────────────────────────
  if (!stamp) return;

  const qreal stampPs = 5.7 * W / 100.0;
  const qreal stampLs = 0.12 * stampPs;
  const qreal padV = 1.6 * W / 100.0;
  const qreal padH = 3.4 * W / 100.0;
  const qreal borderW = std::max(1.0, 0.009 * W);

  QFont stf = theme::mono(static_cast<int>(stampPs));
  stf.setWeight(QFont::Bold);
  stf.setCapitalization(QFont::AllUppercase);
  stf.setLetterSpacing(QFont::AbsoluteSpacing, stampLs);
  p.setFont(stf);

  QFontMetricsF stfm(stf);
  const QString stampText = QStringLiteral("BOOTLEG");
  const qreal textW = stfm.horizontalAdvance(stampText);
  const qreal textH = stfm.height();

  const qreal boxW = textW + 2.0 * padH;
  const qreal boxH = textH + 2.0 * padV;

  // Anchor: top-right of box is ~5% from right, ~14% from top
  const qreal anchorX = W - 0.05 * W; // right edge of the stamp box
  const qreal anchorY = 0.14 * W;     // top edge of the stamp box

  QColor stampColor(168, 44, 8);
  stampColor.setAlphaF(0.66);

  p.save();
  // Translate to the top-right anchor, then rotate +14° about that point
  p.translate(anchorX, anchorY);
  p.rotate(14.0);

  QRectF stampBox(-boxW, 0, boxW, boxH); // right-aligned at origin
  p.setPen(QPen(stampColor, borderW));
  p.setBrush(Qt::NoBrush);
  p.drawRoundedRect(stampBox, 2.0, 2.0);

  p.setPen(stampColor);
  p.drawText(stampBox,
      Qt::AlignHCenter | Qt::AlignVCenter,
      stampText);

  p.restore();
}

// Stripped-down tier for tiny render sizes (queue thumb, player-bar mini),
// where the full sleeve's frame/footer/year/stamp turn to noise. Keeps the
// paper ground and the deterministic accent so a 40px thumb and the big detail
// sleeve for the same album read as the same record.
static auto
paint_monogram(QPainter &p, const QString &artist, const QString &title,
    const QColor &accent, QSizeF sz, int radius) -> void
{
  const qreal W = sz.width();
  const qreal H = sz.height();

  const QColor PAPER(0xf3, 0xef, 0xe7);
  const QColor INK(0x1a, 0x18, 0x15);
  const QColor frameBase(0xd8, 0xd2, 0xc7);
  const QColor markInk = mix(accent, INK, 0.62);
  const QColor frameColor = mix(accent, frameBase, 0.32);

  p.setPen(Qt::NoPen);
  p.setBrush(PAPER);
  QPainterPath bg;
  bg.addRoundedRect(QRectF(QPointF(0, 0), sz), radius, radius);
  p.drawPath(bg);

  // Same inset hairline frame as the sleeve, so both tiers read as one record.
  // render() paints in device px, so snapping the 1px stroke onto the pixel
  // grid (edges centered on a half-pixel) keeps it crisp instead of smeared
  // across two columns by antialiasing — at ~38px that smear reads as aliasing.
  const int frameInset = static_cast<int>(std::round(0.085 * W));
  p.setPen(QPen(frameColor, 1.0));
  p.setBrush(Qt::NoBrush);
  p.drawRect(QRectF(frameInset + 0.5, frameInset + 0.5,
      W - 2.0 * frameInset - 1.0, H - 2.0 * frameInset - 1.0));

  const QString mark = initials(artist, title);
  if (mark.isEmpty()) return;

  // Largest serif that fits the frame interior (leaving room for the rule).
  const qreal interiorW = W - 2.0 * frameInset;
  const qreal interiorH = H - 2.0 * frameInset;
  const qreal ps = fit_font_size(mark, serif_font(10),
      QSizeF(interiorW * 0.86, interiorH * 0.62), 0.22 * W, 0.52 * W, p.device());
  QFont mf = serif_font(ps);
  QFontMetricsF fm(mf);

  // Center the [monogram + gap + rule] block as a unit on the frame's midline.
  const qreal capH = fm.capHeight();
  const qreal gap = 0.11 * H;
  const qreal ruleW = std::min(W * 0.34, fm.horizontalAdvance(mark));
  // Hairline like the frame, so the rule doesn't out-weight it.
  const qreal ruleWeight = 1.0;
  const qreal baseline = H / 2.0 + (capH - gap - ruleWeight / 2.0) / 2.0;
  // Snap the rule onto the pixel grid for the same reason as the frame.
  const qreal ruleY = std::round(baseline + gap) + 0.5;

  p.setFont(mf);
  p.setPen(markInk);
  p.drawText(QPointF((W - fm.horizontalAdvance(mark)) / 2.0, baseline), mark);

  p.setPen(QPen(accent, ruleWeight));
  p.drawLine(QPointF(std::round((W - ruleW) / 2.0), ruleY),
      QPointF(std::round((W + ruleW) / 2.0), ruleY));
}

auto
render(style s, const QString &artist, const QString &title,
    const QString &year, const QColor &accent, QSize size,
    bool stamp, int radius) -> QPixmap
{
  QPixmap px(size);
  px.fill(Qt::transparent);

  QPainter p(&px);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::TextAntialiasing);

  switch (s)
    {
    case style::sleeve:
      paint_sleeve(p, artist, title, year, accent, QSizeF(size), stamp, radius);
      break;
    case style::monogram:
      paint_monogram(p, artist, title, accent, QSizeF(size), radius);
      break;
    }

  p.end();
  return px;
}

} // namespace textcover
