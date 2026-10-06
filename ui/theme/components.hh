#ifndef COMPONENTS_HH
#define COMPONENTS_HH
#include <Qt>

class QFrame;
class QLabel;
class QPushButton;
class QString;
class QWidget;

// Reusable styled components — the single home for each repeated QSS "recipe"
// that was previously copy-pasted across the state-view files. Each factory
// owns exactly one setStyleSheet call; recipes use palette(role) + theme::tok
// values only (no hardcoded hex, font-family, or font-size).
namespace theme::ui {

// Filled accent action button (e.g. "TRY AGAIN", "Reconnect", "RETRY"). Use
// style_as_primary_button to give an existing button (e.g. one from a .ui file)
// the same look.
auto primary_button(const QString &text, QWidget *parent = nullptr) -> QPushButton *;
auto style_as_primary_button(QPushButton *btn) -> void;

// Transparent outlined pill button (e.g. "SETTINGS", "CANCEL", "Dismiss"). Use
// style_as_outline_button for an existing button.
auto outline_pill_button(const QString &text, QWidget *parent = nullptr) -> QPushButton *;
auto style_as_outline_button(QPushButton *btn) -> void;

// Back / return-to-previous-view navigation button: a leading chevron icon
// plus a destination label (e.g. "Library", "Back"). Sits top-left of a
// full-screen view. Use style_as_back_button to give an existing button
// (e.g. one defined in a .ui file) the same look.
auto back_button(const QString &label, QWidget *parent = nullptr) -> QPushButton *;
auto style_as_back_button(QPushButton *btn, const QString &label) -> void;

// Bordered metadata chip label (e.g. the server/detail chips). Pass
// compact=true for a smaller inline tag (e.g. a type badge beside a name).
auto chip(const QString &text, QWidget *parent = nullptr, bool compact = false) -> QLabel *;

// Surface card: Base background, large radius. Pass bordered=false to omit
// the hairline border (e.g. for a card floating over a dimmed/blurred scrim,
// where the fill color alone already reads as a distinct elevated surface).
auto card(QWidget *parent = nullptr, bool bordered = true) -> QFrame *;

// Uppercase mono section header label (e.g. "UP NEXT", "AUDIO").
auto section_header(const QString &text, QWidget *parent = nullptr) -> QLabel *;

// Hairline separator rule in the requested orientation.
auto separator(Qt::Orientation orientation, QWidget *parent = nullptr) -> QFrame *;

// Soft downward drop shadow for album covers, sized proportionally to the
// cover's edge length so every cover surface reads as the same recipe. Applies
// a QGraphicsDropShadowEffect to target. (The library grid uses its own painted
// shadow inside the item delegate and is intentionally not covered here.)
auto cover_shadow(QWidget *target, int cover_px) -> void;

} // namespace theme::ui

#endif // COMPONENTS_HH
