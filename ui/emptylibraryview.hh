#ifndef EMPTYLIBRARYVIEW_HH
#define EMPTYLIBRARYVIEW_HH

#include <QWidget>

// Shown inside the library content area when the backend is connected and the
// library loaded, but contains no albums. The connect / loading / error states
// are full-area overlays owned by ConnectionStateController; this is a quiet
// in-content placeholder for the "connected, just empty" case.
class EmptyLibraryView : public QWidget {
  Q_OBJECT

public:
  explicit EmptyLibraryView(QWidget *parent = nullptr);

signals:
  void refresh();
};

#endif // EMPTYLIBRARYVIEW_HH
