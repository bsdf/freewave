#include <QApplication>

#include "shaderlabwindow.hh"
#include "ui/theme.hh"

// Standalone live shader test harness — hosts the real Now Playing QML scene and
// hot-reloads edited res/shaders/*.frag. Built only with -DBUILD_SHADER_LAB=ON.
auto
main(int argc, char **argv) -> int
{
  QApplication app{argc, argv};
  QCoreApplication::setApplicationName(QStringLiteral("freewave-shader-lab"));

  theme::load_fonts();
  QApplication::setPalette(theme::make_palette());
  app.setStyleSheet(theme::build_stylesheet());

  ShaderLabWindow w;
  w.resize(1280, 800);
  w.show();
  return app.exec();
}
