#ifndef LOGWINDOW_HH
#define LOGWINDOW_HH

#include <QWidget>
class QPlainTextEdit;

class LogWindow : public QWidget {
  Q_OBJECT
public:
  explicit LogWindow(QWidget *parent = nullptr);
  auto text_edit() -> QPlainTextEdit *;

private slots:
  void on_text_changed();

private:
  QPlainTextEdit *edit;
  bool auto_scroll = true;
};

#endif
