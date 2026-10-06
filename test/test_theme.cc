// Golden characterization tests for the theme layer.
//
// These lock the EXACT color output of make_palette() and queue_palette(true)
// to their current values. The upcoming token/typography refactor must keep
// these green — a passing run proves the refactor is output-identical, so the
// color migration is verifiable rather than eyeballed.

#include <gtest/gtest.h>
#include <QColor>
#include <QFontDatabase>
#include <QPalette>

#include <QFrame>
#include <QLabel>
#include <QPushButton>

#include "ui/theme.hh"
#include "ui/theme/components.hh"

namespace {

// Compare one palette role against an expected RGB. Reports the role number on
// failure so a mismatch points at the offending setColor.
void
expect_role(const QPalette &p, QPalette::ColorRole role, QColor expected)
{
  const QColor got = p.color(QPalette::Active, role);
  EXPECT_EQ(got, expected)
      << "role " << static_cast<int>(role) << ": got "
      << got.name().toStdString() << " expected "
      << expected.name().toStdString();
}

} // namespace

// ---------------------------------------------------------------------------
// make_palette() — 16 roles, light theme
// ---------------------------------------------------------------------------

TEST(ThemeGolden, MakePalette_AllRoles)
{
  const QPalette p = theme::make_palette();

  const QColor bg{0xf0, 0xf3, 0xf7};
  const QColor bg_alt{0xff, 0xff, 0xff};
  const QColor fg{0x0f, 0x19, 0x23};
  const QColor dim{0x5a, 0x6a, 0x7a};
  const QColor dim2{0x8c, 0x9d, 0xae};
  const QColor hair{0xdc, 0xe4, 0xef};
  const QColor hover_bg{0xe8, 0xee, 0xf6};
  const QColor accent{0xb2, 0x4d, 0x7a};

  expect_role(p, QPalette::Window, bg);
  expect_role(p, QPalette::Base, QColor(Qt::white));
  expect_role(p, QPalette::AlternateBase, bg_alt);
  expect_role(p, QPalette::WindowText, fg);
  expect_role(p, QPalette::Text, fg);
  expect_role(p, QPalette::Button, hover_bg);
  expect_role(p, QPalette::ButtonText, fg);
  expect_role(p, QPalette::Light, bg);
  expect_role(p, QPalette::Midlight, hair);
  expect_role(p, QPalette::Mid, dim2);
  expect_role(p, QPalette::Dark, dim);
  expect_role(p, QPalette::Shadow, hair);
  expect_role(p, QPalette::Highlight, accent);
  expect_role(p, QPalette::HighlightedText, bg);
  expect_role(p, QPalette::PlaceholderText, dim2);
  expect_role(p, QPalette::Link, hover_bg);
}

// ---------------------------------------------------------------------------
// queue_palette(true) — 12 roles, dark queue variant
// ---------------------------------------------------------------------------

TEST(ThemeGolden, QueuePaletteDark_AllRoles)
{
  const QPalette p = theme::queue_palette(true);

  expect_role(p, QPalette::Window, QColor(0x15, 0x1d, 0x2e));
  expect_role(p, QPalette::Base, QColor(0x15, 0x1d, 0x2e));
  expect_role(p, QPalette::AlternateBase, QColor(0x1a, 0x22, 0x33));
  expect_role(p, QPalette::WindowText, QColor(0xdc, 0xe8, 0xf4));
  expect_role(p, QPalette::Text, QColor(0xdc, 0xe8, 0xf4));
  expect_role(p, QPalette::Dark, QColor(0x5a, 0x70, 0x90));
  expect_role(p, QPalette::Mid, QColor(0x5a, 0x70, 0x90));
  expect_role(p, QPalette::Midlight, QColor(0x24, 0x2f, 0x42));
  expect_role(p, QPalette::Button, QColor(0x1e, 0x2a, 0x3d));
  expect_role(p, QPalette::Highlight, QColor(0xb2, 0x4d, 0x7a));
  expect_role(p, QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
  expect_role(p, QPalette::PlaceholderText, QColor(0x5a, 0x70, 0x90));
}

// ---------------------------------------------------------------------------
// queue_palette(false) must be identical to make_palette()
// ---------------------------------------------------------------------------

TEST(ThemeGolden, QueuePaletteLight_EqualsMakePalette)
{
  EXPECT_EQ(theme::queue_palette(false), theme::make_palette());
}

// ---------------------------------------------------------------------------
// Bundled fonts must actually be registered (catches the historical bug where
// the QSS referenced families that were never loaded).
// ---------------------------------------------------------------------------

TEST(ThemeFonts, BundledFamiliesPresent)
{
  theme::load_fonts();
  const QStringList fams = QFontDatabase::families();
  EXPECT_TRUE(fams.contains("IBM Plex Sans")) << "IBM Plex Sans not registered";
  EXPECT_TRUE(fams.contains("IBM Plex Mono")) << "IBM Plex Mono not registered";
}

// ---------------------------------------------------------------------------
// Typography catalog — pins the named styles' family/size/weight so call-site
// migration is provably visually neutral.
// ---------------------------------------------------------------------------

namespace {

void
expect_font(const QFont &f, const QString &family, int px, int weight)
{
  EXPECT_TRUE(f.families().contains(family))
      << "family: got " << f.families().join(",").toStdString()
      << " expected to contain " << family.toStdString();
  EXPECT_EQ(f.pixelSize(), px);
  EXPECT_EQ(f.weight(), weight);
}

} // namespace

TEST(ThemeType, Catalog)
{
  expect_font(theme::type::track_title(true), "IBM Plex Sans", 12, QFont::Medium);
  expect_font(theme::type::track_title(false), "IBM Plex Sans", 12, QFont::Normal);
  expect_font(theme::type::display_sm(true), "IBM Plex Sans", 10, QFont::Medium);
  expect_font(theme::type::display_sm(false), "IBM Plex Sans", 10, QFont::Normal);
  expect_font(theme::type::body(true), "IBM Plex Sans", 13, QFont::Medium);
  expect_font(theme::type::body(false), "IBM Plex Sans", 13, QFont::Normal);
  expect_font(theme::type::mono_meta(), "IBM Plex Mono", 10, QFont::Normal);
  expect_font(theme::type::section_label(), "IBM Plex Mono", 9, QFont::Medium);
}

// ---------------------------------------------------------------------------
// Component library — each factory returns a non-null widget with the expected
// text and an identifying objectName. Rendered QSS can't be golden-tested, so
// this pins the construction contract; visual neutrality is verified by review.
// ---------------------------------------------------------------------------

TEST(ThemeComponents, Factories)
{
  auto *primary = theme::ui::primary_button("TRY AGAIN");
  ASSERT_NE(primary, nullptr);
  EXPECT_EQ(primary->text(), "TRY AGAIN");
  EXPECT_EQ(primary->objectName(), "fwPrimaryButton");

  auto *outline = theme::ui::outline_pill_button("CANCEL");
  ASSERT_NE(outline, nullptr);
  EXPECT_EQ(outline->text(), "CANCEL");
  EXPECT_EQ(outline->objectName(), "fwOutlinePill");

  auto *chip = theme::ui::chip("localhost:6600");
  ASSERT_NE(chip, nullptr);
  EXPECT_EQ(chip->text(), "localhost:6600");
  EXPECT_EQ(chip->objectName(), "fwChip");

  auto *card = theme::ui::card();
  ASSERT_NE(card, nullptr);
  EXPECT_EQ(card->objectName(), "fwCard");

  auto *header = theme::ui::section_header("UP NEXT");
  ASSERT_NE(header, nullptr);
  EXPECT_EQ(header->text(), "UP NEXT");
  EXPECT_EQ(header->objectName(), "fwSectionHeader");

  // Canonical hairline: a 1px Midlight-filled NoFrame, thin in its orientation.
  auto *sep = theme::ui::separator(Qt::Horizontal);
  ASSERT_NE(sep, nullptr);
  EXPECT_EQ(sep->frameShape(), QFrame::NoFrame);
  EXPECT_EQ(sep->objectName(), "fwSeparator");
  EXPECT_EQ(sep->backgroundRole(), QPalette::Midlight);
  EXPECT_EQ(sep->maximumHeight(), 1);

  auto *vsep = theme::ui::separator(Qt::Vertical);
  ASSERT_NE(vsep, nullptr);
  EXPECT_EQ(vsep->maximumWidth(), 1);

  delete primary;
  delete outline;
  delete chip;
  delete card;
  delete header;
  delete sep;
  delete vsep;
}

// ---------------------------------------------------------------------------
// Generated global stylesheet — rendered QSS can't be golden-tested, so pin the
// token-derived geometry and the key palette(role) refs. A future tok:: change
// must flow through here, guarding against the sheet drifting back to literals.
// ---------------------------------------------------------------------------

TEST(ThemeStylesheet, TokensInterpolated)
{
  const QString qss = theme::build_stylesheet();

  // Radius comes from tok::rMd, scrollbar geometry from tok::sm / tok::lg.
  EXPECT_TRUE(qss.contains(QString("border-radius: %1px").arg(int(theme::tok::rMd))));
  EXPECT_TRUE(qss.contains(QString("width: %1px").arg(int(theme::tok::sm))));
  EXPECT_TRUE(qss.contains(QString("min-height: %1px").arg(int(theme::tok::lg))));
  EXPECT_TRUE(qss.contains(QString("min-width: %1px").arg(int(theme::tok::lg))));

  // No placeholder leaked through (all .arg slots were filled).
  EXPECT_FALSE(qss.contains('%'));

  // Colors stay as palette(role) references, not hardcoded.
  EXPECT_TRUE(qss.contains("palette(highlight)"));
  EXPECT_TRUE(qss.contains("palette(midlight)"));
  EXPECT_TRUE(qss.contains("palette(highlighted-text)"));
}
