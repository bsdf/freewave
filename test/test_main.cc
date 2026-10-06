// Custom GTest main that provides a QApplication for all tests that
// exercise Qt's signal/slot, metatype, and pixmap machinery.
//
// QT_QPA_PLATFORM=offscreen is set before QApplication construction so the
// test binary works without a physical display (e.g. CI, SSH sessions).
//
// When compiled with AddressSanitizer, Qt's offscreen platform plugin leaves
// intentional long-lived allocations (QScreen, QPlatformScreen, ...) that
// LSan reports as leaks.  __lsan_default_options() sets exitcode=0 so LSan
// prints the report but exits cleanly, which keeps gtest_discover_tests and
// ctest happy.  This does NOT suppress leaks in our own code — those still
// produce non-zero exit codes via GTest's own exit path.

#include <gtest/gtest.h>
#include <QApplication>

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
extern "C" const char *
__lsan_default_options()
{
  return "exitcode=0";
}
#endif
#endif

int
main(int argc, char **argv)
{
  // Must be set before QApplication is constructed.
  qputenv("QT_QPA_PLATFORM", "offscreen");

  // Tests only ever talk to 127.0.0.1 (HttpTestServer), so host proxy config
  // must not leak in: souphttpsrc reads http_proxy but ignores no_proxy, which
  // breaks the GstIntegration suite on any proxy-configured machine.
  for (const char *var : {"http_proxy", "https_proxy", "HTTP_PROXY",
           "HTTPS_PROXY", "all_proxy", "ALL_PROXY"})
    qunsetenv(var);

  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
