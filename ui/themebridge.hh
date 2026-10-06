#ifndef THEMEBRIDGE_HH
#define THEMEBRIDGE_HH

#include <QColor>
#include <QObject>
#include <QString>

// Read-only bridge exposing the C++ theme token system (theme::tok, theme::*
// fonts) to the QML Now Playing scene, so QML binds the same colors/fonts the
// rest of the app uses instead of re-declaring hex literals and font families.
// Properties are CONSTANT — tokens are fixed at startup. Exposed to QML as the
// `Theme` context object.
class ThemeBridge : public QObject {
  Q_OBJECT
  // Primary font families (mirrors theme::ui_sans / theme::mono). Single string
  // for Text.font.family — the QML font value type rejects an assignable
  // `families` list here, and the bundled Plex fonts always load at startup.
  Q_PROPERTY(QString sansFamily READ sans_family CONSTANT)
  Q_PROPERTY(QString monoFamily READ mono_family CONSTANT)
  // Ambient-wash field tints (theme::tok::npwash) and on-field text color.
  Q_PROPERTY(QColor washNear READ wash_near CONSTANT)
  Q_PROPERTY(QColor washMid READ wash_mid CONSTANT)
  Q_PROPERTY(QColor washFar READ wash_far CONSTANT)
  Q_PROPERTY(QColor textColor READ text_color CONSTANT)

public:
  explicit ThemeBridge(QObject *parent = nullptr);

  auto sans_family() const -> QString;
  auto mono_family() const -> QString;
  auto wash_near() const -> QColor;
  auto wash_mid() const -> QColor;
  auto wash_far() const -> QColor;
  auto text_color() const -> QColor;

  // On-field text color at a given alpha (0.0–1.0). QML uses this instead of
  // hand-rolled Qt.rgba(1, 1, 1, a) literals so the base color stays a token.
  // Conventional return type: moc cannot parse trailing-return on invokables.
  Q_INVOKABLE QColor textAlpha(qreal a) const;
};

#endif // THEMEBRIDGE_HH
