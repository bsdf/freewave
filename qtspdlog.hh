#ifndef QTSPDLOG_HH
#define QTSPDLOG_HH

#include <spdlog/spdlog.h>
#include <spdlog/fmt/ranges.h>

#include <QSize>
#include <QRect>
#include <QEvent>
#include <QPoint>
#include <QString>
#include <QDateTime>
#include <QMetaEnum>

#define FMT_ADD_TYPE(TYPE, FMT_STR, FMT_ARGS...)                                \
  template<>                                                                    \
  struct fmt::formatter<TYPE> : fmt::formatter<std::string> {                   \
    template<typename FormatContext>                                            \
    auto format(const TYPE &x, FormatContext &ctx) const -> decltype(ctx.out()) \
    {                                                                           \
      return fmt::format_to(ctx.out(), FMT_STR, FMT_ARGS);                      \
    }                                                                           \
  };

FMT_ADD_TYPE(QString, "{}", x.toStdString());
FMT_ADD_TYPE(QPoint, "QPoint({}, {})", x.x(), x.y());
FMT_ADD_TYPE(QSize, "QSize({}, {})", x.width(), x.height());
FMT_ADD_TYPE(QRect, "QRect({}, {})", x.topLeft(), x.size());
FMT_ADD_TYPE(QDateTime, "QDateTime({})", x.toString());

FMT_ADD_TYPE(QEvent::Type, "QEvent::{}",
    QEvent::staticMetaObject.enumerator(
                                QEvent::staticMetaObject.indexOfEnumerator("Type"))
        .valueToKey(x));

#endif /* QTSPDLOG_HH */
