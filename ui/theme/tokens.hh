#ifndef TOKENS_HH
#define TOKENS_HH
#include <QColor>
#include <QSize>

namespace theme::tok {

// ── Light brand palette ───────────────────────────────────────
inline const QColor ink{0x0f, 0x19, 0x23};     // fg
inline const QColor mist{0xf0, 0xf3, 0xf7};    // window bg
inline const QColor surface{0xff, 0xff, 0xff}; // base / white
inline const QColor dim{0x5a, 0x6a, 0x7a};
inline const QColor dim2{0x8c, 0x9d, 0xae};
inline const QColor hair{0xdc, 0xe4, 0xef};
inline const QColor hover{0xe8, 0xee, 0xf6};
inline const QColor accent{0xb2, 0x4d, 0x7a}; // plum — TWEAK_DEFAULTS
inline const QColor onAccent{0xff, 0xff, 0xff};
inline const QColor play_hover{0x2e, 0x2d, 0x28}; // play button hover (one-off)

// ── Dark (queue) variant ──────────────────────────────────────
inline const QColor d_bg{0x15, 0x1d, 0x2e};
inline const QColor d_bg_alt{0x1a, 0x22, 0x33};
inline const QColor d_fg{0xdc, 0xe8, 0xf4};
inline const QColor d_dim{0x5a, 0x70, 0x90};
inline const QColor d_midlight{0x24, 0x2f, 0x42};
inline const QColor d_button{0x1e, 0x2a, 0x3d};

// ── Now-Playing "Ambient Wash" art-color field ────────────────
// Dark mix targets the album accent is blended toward (center→edge).
namespace npwash {
inline const QColor tint_near{0x14, 0x17, 0x1f};
inline const QColor tint_mid{0x0b, 0x0d, 0x14};
inline const QColor tint_far{0x09, 0x0b, 0x11};
inline const QColor text{0xff, 0xff, 0xff}; // on-field text (used at varying alpha)
} // namespace npwash

// ── Spacing scale (4px base) ──────────────────────────────────
enum Space { xs = 4,
  sm = 8,
  md = 14,
  lg = 20,
  xl = 32,
  xxl = 40 };

// ── Radius scale ──────────────────────────────────────────────
enum Radius { rSm = 3,
  rMd = 4,
  rLg = 8 };

// ── Type scale (px) and tracking ──────────────────────────────
enum Size { caption = 10,
  body = 13,
  label = 12,
  subhead = 16,
  title = 22,
  hero = 30 };
enum Track { tight = -50,
  normal = 0,
  wide = 100,
  wider = 200 }; // x0.01px

// ── Motion (ms) ───────────────────────────────────────────────
enum Motion { fast = 120,
  base = 200,
  slow = 320 };

// ── Album-art render sizes (square, logical px) ───────────────
namespace art {
inline const QSize mini{38, 38};            // player-bar badge
inline const QSize thumb{40, 40};           // queue-list thumbnail
inline const QSize accent_sample{128, 128}; // accent sampling (no detail needed)
inline const QSize grid{200, 200};          // library-grid default
inline const QSize now_playing{300, 300};   // now-playing hero
// At or below this min dimension the full sleeve turns to noise → monogram tier.
inline constexpr int monogram_max = 64;
} // namespace art

} // namespace theme::tok

#endif // TOKENS_HH
