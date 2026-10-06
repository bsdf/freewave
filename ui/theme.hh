#ifndef THEME_HH
#define THEME_HH
#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QPalette>
#include <QString>
#include "ui/theme/tokens.hh"

namespace theme {

// Load all bundled application fonts. Call once at startup (and in tests).
inline void
load_fonts()
{
  QFontDatabase::addApplicationFont(":/fonts/InstrumentSerif-Regular.ttf");
  QFontDatabase::addApplicationFont(":/fonts/InstrumentSerif-Italic.ttf");
  QFontDatabase::addApplicationFont(":/fonts/JetBrainsMono-Regular.ttf");
  QFontDatabase::addApplicationFont(":/fonts/SpaceGrotesk-Regular.ttf");
  QFontDatabase::addApplicationFont(":/fonts/SpaceGrotesk-Medium.ttf");
  QFontDatabase::addApplicationFont(":/fonts/DMMono-Regular.ttf");
  QFontDatabase::addApplicationFont(":/fonts/DMMono-Medium.ttf");
  QFontDatabase::addApplicationFont(":/fonts/IBMPlexSans-Regular.ttf");
  QFontDatabase::addApplicationFont(":/fonts/IBMPlexSans-Medium.ttf");
  QFontDatabase::addApplicationFont(":/fonts/IBMPlexSans-SemiBold.ttf");
  QFontDatabase::addApplicationFont(":/fonts/IBMPlexMono-Regular.ttf");
  QFontDatabase::addApplicationFont(":/fonts/IBMPlexMono-Medium.ttf");
  QFontDatabase::addApplicationFont(":/fonts/IBMPlexMono-SemiBold.ttf");
  QFontDatabase::addApplicationFont(":/fonts/IBMPlexMono-Bold.ttf");
}

// IBM Plex Sans for display titles (album names, track titles)
inline QFont
display_serif(int px = 17)
{
  QFont f;
  f.setFamilies({"IBM Plex Sans", "Inter", "Liberation Sans"});
  f.setPixelSize(px);
  return f;
}

// IBM Plex Sans for general UI text
inline QFont
ui_sans(int px = 13)
{
  QFont f;
  f.setFamilies({"IBM Plex Sans", "Inter", "Liberation Sans", "DejaVu Sans"});
  f.setPixelSize(px);
  return f;
}

// IBM Plex Mono for track numbers, timestamps, metadata
inline QFont
mono(int px = 10)
{
  QFont f;
  f.setFamilies({"IBM Plex Mono", "JetBrains Mono", "Cascadia Code", "DejaVu Sans Mono"});
  f.setPixelSize(px);
  return f;
}

// ───────────────────────────────────────────────────────────────────────────
// Typography catalog — named, fully-configured text styles.
//
// Each returns a complete QFont (family + size + weight), so call sites stop
// hand-mutating weight and stop inventing sizes. Values match the dominant
// existing usage so migration is visually neutral; sizes track tok::Size where
// they align. Only the genuinely recurring intents live here — true one-offs
// (transport glyphs, onboarding's bespoke tracking) keep using the raw helpers.
// ───────────────────────────────────────────────────────────────────────────
namespace type {

// Display (IBM Plex Sans) — queue/track row titles.
inline QFont
track_title(bool playing)
{
  auto f = display_serif(tok::label); // 12px
  f.setWeight(playing ? QFont::Medium : QFont::Normal);
  return f;
}

// Display (IBM Plex Sans) small — queue subtitles / group labels.
inline QFont
display_sm(bool strong = false)
{
  auto f = display_serif(tok::caption); // 10px
  f.setWeight(strong ? QFont::Medium : QFont::Normal);
  return f;
}

// Sans (IBM Plex Sans) body — list/item titles, form labels.
inline QFont
body(bool strong = false)
{
  auto f = ui_sans(tok::body); // 13px
  f.setWeight(strong ? QFont::Medium : QFont::Normal);
  return f;
}

// Mono (IBM Plex Mono) metadata — track numbers, timestamps, small numerals.
inline QFont
mono_meta()
{
  return mono(tok::caption); // 10px
}

// Mono (IBM Plex Mono) small section header (e.g. "UP NEXT").
inline QFont
section_label()
{
  auto f = mono(9);
  f.setWeight(QFont::Medium);
  return f;
}

} // namespace type

inline QPalette
make_palette()
{
  const QColor &bg = tok::mist;
  const QColor &bg_alt = tok::surface;
  const QColor &fg = tok::ink;
  const QColor &dim = tok::dim;
  const QColor &dim2 = tok::dim2;
  const QColor &hair = tok::hair;
  const QColor &hover_bg = tok::hover;
  const QColor &accent = tok::accent;

  QPalette p;
  p.setColor(QPalette::Window, bg);
  p.setColor(QPalette::Base, Qt::white);
  p.setColor(QPalette::AlternateBase, bg_alt);
  p.setColor(QPalette::WindowText, fg);
  p.setColor(QPalette::Text, fg);
  p.setColor(QPalette::Button, hover_bg);
  p.setColor(QPalette::ButtonText, fg);
  p.setColor(QPalette::Light, bg);
  p.setColor(QPalette::Midlight, hair);
  p.setColor(QPalette::Mid, dim2);
  p.setColor(QPalette::Dark, dim);
  p.setColor(QPalette::Shadow, hair);
  p.setColor(QPalette::Highlight, accent);
  p.setColor(QPalette::HighlightedText, bg);
  p.setColor(QPalette::PlaceholderText, dim2);
  p.setColor(QPalette::Link, hover_bg);
  return p;
}

inline QPalette
queue_palette(bool dark)
{
  if (!dark)
    return make_palette();

  QPalette p;
  p.setColor(QPalette::Window, tok::d_bg);
  p.setColor(QPalette::Base, tok::d_bg);
  p.setColor(QPalette::AlternateBase, tok::d_bg_alt);
  p.setColor(QPalette::WindowText, tok::d_fg);
  p.setColor(QPalette::Text, tok::d_fg);
  p.setColor(QPalette::Dark, tok::d_dim);
  p.setColor(QPalette::Mid, tok::d_dim);
  p.setColor(QPalette::Midlight, tok::d_midlight);
  p.setColor(QPalette::Button, tok::d_button);
  p.setColor(QPalette::Highlight, tok::accent);
  p.setColor(QPalette::HighlightedText, tok::onAccent);
  p.setColor(QPalette::PlaceholderText, tok::d_dim);
  return p;
}

// Build the global structural stylesheet. Colors stay as palette(role); only
// geometry that belongs to a design scale is interpolated from tok:: so QSS
// radius/spacing track the same constants the C++ layouts use.
//   %1 = rMd  border radius (line edit, scrollbar handles, buttons)
//   %2 = sm   scrollbar thickness
//   %3 = lg   scrollbar handle min length
// Values left as literals (5/6/10px control padding, slider groove 3px / pill
// radii, scrollbar handle 2px insets) are element-intrinsic geometry, not scale
// tokens — promoting them would only add noise to tokens.hh.
inline auto
build_stylesheet() -> QString
{
  return QString(R"(
QWidget {
  font-family: "IBM Plex Sans", "Inter", "Liberation Sans", sans-serif;
}

QPlainTextEdit#log_view {
  font-family: "IBM Plex Mono", "JetBrains Mono", "Cascadia Code", "DejaVu Sans Mono", monospace;
  font-size: 12px;
}

QFrame[frameShape="4"], QFrame[frameShape="5"] {
  border: 1px solid palette(midlight);
}

QLineEdit {
  background: palette(base);
  border: 1px solid palette(midlight);
  border-radius: %1px;
  padding: 5px 8px;
  selection-background-color: palette(highlight);
  selection-color: palette(highlighted-text);
}

QLineEdit:focus {
  border-color: palette(highlight);
}

QScrollBar:vertical {
  background: transparent;
  width: %2px;
  border: none;
  margin: 0;
}

QScrollBar::handle:vertical {
  background: palette(midlight);
  border-radius: %1px;
  min-height: %3px;
  margin: 0 2px;
}

QScrollBar::handle:vertical:hover {
  background: palette(mid);
}

QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
  height: 0;
}

QScrollBar:horizontal {
  background: transparent;
  height: %2px;
  border: none;
  margin: 0;
}

QScrollBar::handle:horizontal {
  background: palette(midlight);
  border-radius: %1px;
  min-width: %3px;
  margin: 2px 0;
}

QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
  width: 0;
}

QPushButton {
  background: transparent;
  border: none;
  border-radius: %1px;
  padding: 6px 10px;
}

QPushButton:hover {
  background: palette(button);
}

QPushButton:pressed {
  background: palette(midlight);
}

QPushButton:checked {
  background: palette(window-text);
  color: palette(window);
}

QPushButton:flat {
  border: none;
  background: transparent;
}

QPushButton:flat:hover {
  background: palette(button);
}

QLabel {
  background: transparent;
}
)")
      .arg(int(tok::rMd))
      .arg(int(tok::sm))
      .arg(int(tok::lg));
}

} // namespace theme

#endif // THEME_HH
