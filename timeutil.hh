#ifndef TIMEUTIL_HH
#define TIMEUTIL_HH

#include <QDateTime>
#include <QString>

namespace timeutil {
auto ms_to_text(const uint32_t &ms) -> QString;

// A 4-digit year for display, or empty if the date is missing or a zero year.
auto year_label(const QString &date) -> QString;

// A short "3 days ago" / "just now" style label relative to now, or empty if
// `when` is invalid.
auto relative_label(const QDateTime &when) -> QString;
}

#endif /* TIMEUTIL_HH */
