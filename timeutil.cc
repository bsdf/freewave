#include "timeutil.hh"

#include <QTime>

constexpr auto HOUR_MSECS = 60 * 60 * 1000;

auto
timeutil::ms_to_text(const uint32_t &ms) -> QString
{
  auto time = QTime::fromMSecsSinceStartOfDay(ms);
  auto time_fmt = ms >= HOUR_MSECS
                      ? "h:mm:ss"
                      : "m:ss";
  return time.toString(time_fmt);
}

auto
timeutil::year_label(const QString &date) -> QString
{
  // Dates may be a bare year ("2021") or full ("2021-05-01"); take the year.
  QString year = date.trimmed().left(4);
  bool ok = false;
  if (year.toInt(&ok) == 0 || !ok)
    return {};
  return year;
}

auto
timeutil::relative_label(const QDateTime &when) -> QString
{
  if (!when.isValid())
    return {};

  auto secs = when.secsTo(QDateTime::currentDateTime());
  if (secs < 0)
    secs = 0;

  auto unit_label = [](qint64 count, const char *unit) {
    return QString("%1 %2%3 ago").arg(count).arg(unit).arg(count == 1 ? "" : "s");
  };

  if (secs < 60)
    return "just now";
  if (secs < 60 * 60)
    return unit_label(secs / 60, "minute");
  if (secs < 24 * 60 * 60)
    return unit_label(secs / (60 * 60), "hour");
  if (secs < 30 * 24 * 60 * 60)
    return unit_label(secs / (24 * 60 * 60), "day");
  if (secs < 365 * 24 * 60 * 60)
    return unit_label(secs / (30 * 24 * 60 * 60), "month");
  return unit_label(secs / (365LL * 24 * 60 * 60), "year");
}
