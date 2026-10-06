#include "logwindow.hh"

#include <QAction>
#include <QIcon>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QToolBar>
#include <QVBoxLayout>

LogWindow::LogWindow(QWidget *parent)
  : QWidget(parent, Qt::Window)
{
  setWindowTitle("Log");
  resize(700, 400);

  edit = new QPlainTextEdit(this);
  edit->setReadOnly(true);
  edit->setLineWrapMode(QPlainTextEdit::NoWrap);
  // The global "QWidget { font-family: ... }" sheet rule overrides setFont(), so
  // the monospace font is applied via a targeted rule in theme::build_stylesheet().
  edit->setObjectName("log_view");

  auto *scroll_action = new QAction(QIcon::fromTheme("go-bottom"), "Auto-scroll", this);
  scroll_action->setCheckable(true);
  scroll_action->setChecked(true);
  scroll_action->setToolTip("Auto-scroll to bottom");
  connect(scroll_action, &QAction::toggled, this, [this](bool on) {
    auto_scroll = on;
  });

  auto *wrap_action = new QAction(QIcon::fromTheme("text-wrap"), "Word wrap", this);
  wrap_action->setCheckable(true);
  wrap_action->setChecked(false);
  wrap_action->setToolTip("Word wrap");
  connect(wrap_action, &QAction::toggled, this, [this](bool on) {
    edit->setLineWrapMode(on ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
  });

  auto *toolbar = new QToolBar(this);
  toolbar->setMovable(false);
  toolbar->addAction(scroll_action);
  toolbar->addAction(wrap_action);

  connect(edit, &QPlainTextEdit::textChanged, this, &LogWindow::on_text_changed);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(toolbar);
  layout->addWidget(edit);
}

auto
LogWindow::on_text_changed() -> void
{
  if (auto_scroll)
    edit->verticalScrollBar()->setValue(edit->verticalScrollBar()->maximum());
}

auto
LogWindow::text_edit() -> QPlainTextEdit *
{
  return edit;
}
