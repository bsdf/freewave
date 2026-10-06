#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QSettings>

#include <spdlog/spdlog.h>

#include "ui/mainwindow.hh"
#include "ui/onboardingdialog.hh"
#include "ui/theme.hh"
#include "version.hh"

#include <cstdio>
#include <ranges>
#include <span>
#include <signal.h>

#define APP_ORG    "xeyes.org"
#define APP_DOMAIN "xeyes.org"
#define APP_NAME   "freewave"

static auto
print_version() -> void
{
  std::printf("freewave %s\n", FREEWAVE_VERSION);
  std::printf("backends:"
#ifdef ENABLE_MPD
              " mpd"
#endif
#ifdef ENABLE_SUBSONIC
              " subsonic"
#endif
              "\n");
  std::printf("features:"
#ifdef ENABLE_VISUALIZER
              " visualizer"
#endif
#ifdef ENABLE_MPRIS
              " mpris"
#endif
#ifdef ENABLE_KEYCHAIN
              " keychain"
#endif
              "\n");
}

auto
main(int argc, char **argv) -> int
{
  // Parsed via a plain QStringList, not process(QCoreApplication&): this must
  // resolve before QApplication is constructed below, so --version needs no
  // display or session bus — release CI smoke-checks the built bundle that way.
  auto args = std::span(argv, argc)
              | std::views::transform([](const char *a) { return QString::fromLocal8Bit(a); })
              | std::ranges::to<QStringList>();

  QCommandLineParser parser;
  QCommandLineOption version_opt("version", "Print version, backends, and enabled features.");
  parser.addOption(version_opt);
  parser.parse(args);

  if (parser.isSet(version_opt))
    {
      print_version();
      return 0;
    }

  // mpd often kills its socket, so ignore this signal
  signal(SIGPIPE, SIG_IGN);

  // Font fallback logs every family that can't shape a script; not actionable.
  // QT_LOGGING_RULES is applied after this and still overrides it.
  QLoggingCategory::setFilterRules("qt.text.font.db=false");

  QApplication a{argc, argv};
  QCoreApplication::setOrganizationName(APP_ORG);
  QCoreApplication::setOrganizationDomain(APP_DOMAIN);
  QCoreApplication::setApplicationName(APP_NAME);
  QCoreApplication::setApplicationVersion(FREEWAVE_VERSION);
  QGuiApplication::setDesktopFileName(QStringLiteral("org.xeyes.Freewave"));

  theme::load_fonts();

  QApplication::setPalette(theme::make_palette());
  a.setStyleSheet(theme::build_stylesheet());

  QSettings settings;

  if (!settings.contains("setup_complete"))
    {
      OnboardingDialog dlg;
      if (dlg.exec() != QDialog::Accepted)
        return 0;
    }

  MainWindow w;
  w.show();
  return a.exec();
}
